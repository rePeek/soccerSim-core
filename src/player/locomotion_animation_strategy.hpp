#ifndef FOOTBALL_SIM_PLAYER_LOCOMOTION_ANIMATION_STRATEGY_HPP
#define FOOTBALL_SIM_PLAYER_LOCOMOTION_ANIMATION_STRATEGY_HPP

#include <cmath>
#include <limits>
#include <span>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/selector.hpp"
#include "football/animation/types.hpp"
#include "player/player_kinematics.hpp"
#include "player/player_locomotion_animation.hpp"

// LOCO-2.3a — pure, deterministic locomotion-animation selection strategy.
//
// Reads only LocomotionAnimationInput (a value snapshot) and the baked clip
// list. It never runs the motion model, never writes a world position, and has
// no Simulation/Humanoid/RNG dependency. It reuses BakedAnimationSelector for
// the coarse Movement/incoming-velocity filter, then ranks the survivors with a
// deterministic score instead of re-implementing the selector.

struct LocomotionAnimationChoice {
  AnimationId animation_id = -1;
  float reference_speed = 0.0f;  // clip mean speed from root_positions
  float speed_error = 0.0f;      // matched error (velocity or cruise speed)
  float playback_rate = 1.0f;    // reference-only rate for LOCO-2.3c
};

namespace locomotion_strategy_detail {

inline float Deg(float rad) { return rad * 180.0f / football::math::pi; }

// Mean reference speed of a clip from its baked root trajectory.
inline float ClipReferenceSpeed(const AnimationClip& clip) {
  if (clip.root_positions.size() < 2 || clip.frame_count == 0) return 0.0f;
  const auto d = clip.root_positions.back() - clip.root_positions.front();
  const float dist = std::sqrt(d.coords[0] * d.coords[0] + d.coords[1] * d.coords[1]);
  return dist / (static_cast<float>(clip.frame_count) / 100.0f);
}

// Heading change implied by the clip metadata, mirroring the selector's use of
// outgoing_direction rotated by outgoing_body_angle.
inline float ClipTurnDeg(const AnimationClip& clip) {
  const auto& m = clip.metadata;
  const auto out = m.outgoing_direction.GetRotated2D(m.outgoing_body_angle);
  return Deg(std::fabs(static_cast<float>(m.incoming_body_direction.GetAngle2D(out))));
}

inline float AngleBetweenDeg(const football::math::Vector3& a, const football::math::Vector3& b) {
  return Deg(std::fabs(static_cast<float>(a.GetAngle2D(b))));
}

}  // namespace locomotion_strategy_detail

inline LocomotionAnimationChoice SelectLocomotionAnimation(
    const LocomotionAnimationInput& input, const std::vector<AnimationClip>& clips) {
  using namespace locomotion_strategy_detail;

  const float actual_speed = input.next.velocity.GetLength();
  const float desired_speed = input.desired_velocity.GetLength();
  // Direction reference: velocity direction while moving; a stable body facing
  // otherwise, so near-zero speed never jitters on a degenerate normalisation.
  const auto stable_dir = input.next.bodyFacing.GetNormalized(Vector3(0.0f, -1.0f, 0.0f));
  const auto motion_dir =
      actual_speed > 1e-3f ? input.next.velocity.GetNormalized(stable_dir) : stable_dir;
  const auto desired_dir = input.desired_velocity.GetNormalized(motion_dir);

  // Three regimes driven by the requested vs actual speed, so the strategy
  // sees "about to accelerate / about to brake / holding speed" rather than
  // only the already-committed speed.
  enum class Trend { Accel, Decel, Cruise };
  Trend trend = Trend::Cruise;
  if (desired_speed > actual_speed + 0.5f) trend = Trend::Accel;
  else if (desired_speed < actual_speed - 0.5f) trend = Trend::Decel;

  // Coarse filter: Movement clips whose incoming velocity is compatible with
  // the committed speed. Non-strict, so the continuous ranking below still sees
  // the adjacent buckets (e.g. 6 m/s ranks walk 5 over sprint 8).
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = FloatToEnumVelocity(actual_speed);
  DataSet candidates;
  BakedAnimationSelector::CrudeSelection(clips, query, candidates);

  LocomotionAnimationChoice best;
  best.animation_id = -1;
  best.speed_error = std::numeric_limits<float>::infinity();
  float best_score = std::numeric_limits<float>::infinity();

  const float desired_turn =
      AngleBetweenDeg(input.current.GetSpeed() > 1e-3f
                         ? input.current.velocity.GetNormalized(motion_dir)
                         : stable_dir,
                     desired_dir);

  for (const int id : candidates) {
    if (id < 0 || static_cast<std::size_t>(id) >= clips.size()) continue;
    const AnimationClip& clip = clips[static_cast<std::size_t>(id)];
    const auto& m = clip.metadata;
    const float clip_speed_delta = m.outgoing_velocity - m.incoming_velocity;
    const float turn_err = std::fabs(ClipTurnDeg(clip) - desired_turn);

    float velocity_error = 0.0f;
    switch (trend) {
      case Trend::Accel:
        // Prefer a clip that ends near the requested speed and actually speeds up.
        velocity_error = std::fabs(m.outgoing_velocity - desired_speed);
        if (clip_speed_delta <= 0.1f) velocity_error += 10.0f;
        break;
      case Trend::Decel:
        velocity_error = std::fabs(m.outgoing_velocity - desired_speed);
        if (clip_speed_delta >= -0.1f) velocity_error += 10.0f;
        break;
      case Trend::Cruise:
        velocity_error = std::fabs(ClipReferenceSpeed(clip) - actual_speed);
        if (std::fabs(clip_speed_delta) > 0.3f) velocity_error += 10.0f;
        break;
    }

    const float score = velocity_error + 0.02f * turn_err;
    if (score < best_score || (score == best_score && id < best.animation_id)) {
      best_score = score;
      best.animation_id = id;
      best.reference_speed = ClipReferenceSpeed(clip);
      best.speed_error = velocity_error;
      best.playback_rate = best.reference_speed > 1e-3f ? actual_speed / best.reference_speed : 1.0f;
    }
  }
  return best;
}

#endif  // FOOTBALL_SIM_PLAYER_LOCOMOTION_ANIMATION_STRATEGY_HPP