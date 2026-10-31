#ifndef FOOTBALL_SIM_PLAYER_FOOT_IK_HPP
#define FOOTBALL_SIM_PLAYER_FOOT_IK_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "football/foundation/math/vector3.hpp"

// P3b core: support-foot anchors and a deterministic two-bone leg solve.
//
// This module is deliberately independent of Animation, Contact and Runtime.
// The caller supplies world-space ankle positions and support flags; it receives
// world-space knee/ankle targets and a released flag when the anchor is outside
// the leg's reach. Pose/Final Pose integration consumes these targets.
enum class FootContactPhase : std::uint8_t {
  Swing,
  Planting,
  Planted,
  Releasing,
};

struct PlayerFootPlantState {
  bool left_planted = false;
  bool right_planted = false;
  football::math::Vector3 left_anchor = football::math::Vector3(0);
  football::math::Vector3 right_anchor = football::math::Vector3(0);
  FootContactPhase left_phase = FootContactPhase::Swing;
  FootContactPhase right_phase = FootContactPhase::Swing;
};

struct PlayerFootPlantInput {
  football::math::Vector3 left_ankle = football::math::Vector3(0);
  football::math::Vector3 right_ankle = football::math::Vector3(0);
  bool left_support = false;
  bool right_support = false;
  // Feet at or below ground + this tolerance may plant.
  float plant_height = 0.16f;
};

struct PlayerFootIkTargets {
  bool has_left = false;
  bool has_right = false;
  football::math::Vector3 left = football::math::Vector3(0);
  football::math::Vector3 right = football::math::Vector3(0);
};

// STRIDE-0: the slide the final pose has to remove, measured on the ankle the
// animation itself produced. A planted foot whose ankle has drifted from its
// anchor is exactly the sliding step this pipeline must not ship. Only the
// horizontal distance counts: lifting a foot is not sliding.
struct PlayerFootPlantSlip {
  float left = 0.0f;
  float right = 0.0f;
};

inline PlayerFootPlantSlip MeasureFootPlantSlip(
    const PlayerFootPlantState& plant,
    const football::math::Vector3& left_ankle,
    const football::math::Vector3& right_ankle) {
  PlayerFootPlantSlip slip;
  if (plant.left_planted) {
    slip.left = (left_ankle - plant.left_anchor).Get2D().GetLength();
  }
  if (plant.right_planted) {
    slip.right = (right_ankle - plant.right_anchor).Get2D().GetLength();
  }
  return slip;
}

// Which foot is carrying weight?
//
// A planted foot travels *backwards* relative to the body at the body's speed,
// while a swinging foot travels forwards. That is a clip-intrinsic signal: it
// does not depend on the playback rate or on the achieved speed, and it survives
// a real clip whose stance foot slides a little inside the animation.
//
// The height-only heuristic that came before cannot tell a stance foot from a
// foot that merely hangs lower; on authored run clips it mislabels half a cycle
// and drags the leg toward a stale anchor. This replaces it until the baked
// per-frame support track exists; the interface is the same question, so the
// track can answer it later without touching the callers.
//
// `backward_rate` is metres of backward hip-relative travel since the previous
// tick, along the body's facing axis.
struct PlayerFootSupportInput {
  float left_backward_rate = 0.0f;
  float right_backward_rate = 0.0f;
  bool have_previous = false;
  bool previous_support_left = false;
  bool have_previous_support = false;
  // A handover needs the other foot to be clearly more planted (m per tick).
  float switch_margin = 0.01f;
};

// A foot moving faster than this in the world is not standing still, whatever a
// support track says: a turning body pivots the foot instead of planting it.
inline constexpr float kStanceMaxWorldSpeed = 2.5f;

struct PlayerFootSupportResult {
  bool left_support = false;
  bool right_support = false;
};

inline PlayerFootSupportResult SelectFootSupport(
    const PlayerFootSupportInput& input) {
  PlayerFootSupportResult result;
  // The first tick has no comparison yet: no foot is claimed, which is the safe
  // answer (a plant is established on a later tick, never guessed).
  if (!input.have_previous) return result;

  float left = input.left_backward_rate;
  float right = input.right_backward_rate;
  if (input.have_previous_support) {
    // The current support keeps a margin, so a crossover does not chatter.
    if (input.previous_support_left) {
      left += input.switch_margin;
    } else {
      right += input.switch_margin;
    }
  }
  const bool left_wins = left > right;
  const bool right_wins = right > left;
  // Winning on a margin is not enough: only a foot that is really travelling
  // backwards is carrying weight. Both feet forward means flight.
  if (left_wins && input.left_backward_rate > 0.0f) result.left_support = true;
  if (right_wins && input.right_backward_rate > 0.0f) result.right_support = true;
  return result;
}

// What to do when a support anchor is no longer reachable.
//
// Dropping the anchor outright un-pins the leg, but it also snaps the pose back
// to the raw gait - which is exactly what a leg pinned at maximum extension was
// hiding. Sliding the anchor toward the foot by a bounded amount keeps the leg
// off the reach limit without a discontinuity: the foot ends up moving, which is
// the honest outcome when the body was pushed away from it.
//
//   residual <= release_residual   ordinary discretisation: keep the anchor
//   release_residual..hard_release slide the anchor toward the foot, bounded
//   above hard_release             a discontinuity (teleport/reset): release
inline constexpr float kPlantReleaseResidual = 0.06f;
// Bounded slide, in metres per tick (3 cm = 3 m/s of anchor travel).
inline constexpr float kPlantRelaxSlide = 0.03f;
// Beyond this the pose is not a support problem any more.
inline constexpr float kPlantHardReleaseResidual = 0.35f;

struct PlayerFootPlantRelax {
  PlayerFootPlantState plant;
  bool left_released = false;
  bool right_released = false;
  float left_slide = 0.0f;
  float right_slide = 0.0f;
};

inline PlayerFootPlantRelax RelaxFootPlant(
    const PlayerFootPlantState& plant, const PlayerFootIkTargets& targets,
    float left_residual, float right_residual,
    float release_residual = kPlantReleaseResidual,
    float max_slide = kPlantRelaxSlide,
    float hard_release = kPlantHardReleaseResidual) {
  PlayerFootPlantRelax result;
  result.plant = plant;
  const auto relax = [&](bool planted, float residual, bool has_target,
                         const football::math::Vector3& foot, bool& released,
                         float& slide, bool& flag, football::math::Vector3& anchor) {
    if (!planted) return;
    if (residual <= release_residual) return;
    if (residual > hard_release || !has_target) {
      released = true;
      flag = false;
      anchor = football::math::Vector3(0);
      return;
    }
    const football::math::Vector3 to_foot = foot - anchor;
    const float distance = to_foot.GetLength();
    if (distance <= 1e-6f) return;
    const float step = std::min(std::min(residual - release_residual, max_slide),
                                distance);
    anchor += to_foot * (step / distance);
    slide = step;
  };
  relax(plant.left_planted, left_residual, targets.has_left, targets.left,
        result.left_released, result.left_slide, result.plant.left_planted,
        result.plant.left_anchor);
  relax(plant.right_planted, right_residual, targets.has_right, targets.right,
        result.right_released, result.right_slide, result.plant.right_planted,
        result.plant.right_anchor);
  if (result.left_released) result.plant.left_phase = FootContactPhase::Releasing;
  if (result.right_released) result.plant.right_phase = FootContactPhase::Releasing;
  return result;
}

// The support authority: a planted foot is released from whatever the gait
// wanted and targets the anchor it was planted on. This is the only place
// support overrides the stride, so shortening a stride can never stretch or
// drag a stance phase, and no two systems write the same foot.
inline PlayerFootIkTargets ApplyFootPlantLocks(
    const PlayerFootPlantState& plant, PlayerFootIkTargets targets) {
  if (plant.left_planted) {
    targets.has_left = true;
    targets.left = plant.left_anchor;
  }
  if (plant.right_planted) {
    targets.has_right = true;
    targets.right = plant.right_anchor;
  }
  return targets;
}

struct PlayerTwoBoneResult {
  football::math::Vector3 knee = football::math::Vector3(0);
  football::math::Vector3 ankle = football::math::Vector3(0);
  bool reachable = false;
  bool released = false;
};

class PlayerFootIk final {
 public:
  // Keeps the current knee bend plane, preserves both bone lengths and clamps
  // an over-extended target to the leg's reachable sphere.
  static inline PlayerTwoBoneResult Solve(
      const football::math::Vector3& hip,
      const football::math::Vector3& knee,
      const football::math::Vector3& ankle,
      const football::math::Vector3& target) {
    using football::math::Vector3;
    PlayerTwoBoneResult result;
    const float upper = (knee - hip).GetLength();
    const float lower = (ankle - knee).GetLength();
    if (upper <= 1e-6f || lower <= 1e-6f) {
      result.knee = knee;
      result.ankle = ankle;
      return result;
    }
    const float max_reach = upper + lower - 1e-4f;
    const float min_reach = std::fabs(upper - lower) + 1e-4f;
    Vector3 goal = target;
    Vector3 direction = goal - hip;
    float distance = direction.GetLength();
    if (distance <= 1e-6f) {
      goal = hip + Vector3(0, 0, -max_reach);
    } else if (distance < min_reach) {
      goal = hip + direction * (min_reach / distance);
    } else if (distance > max_reach) {
      goal = hip + direction * (max_reach / distance);
    }
    result.released = (goal - target).GetLength() > 1e-5f;
    direction = goal - hip;
    distance = direction.GetLength();
    if (distance <= 1e-6f) {
      result.knee = knee;
      result.ankle = ankle;
      return result;
    }
    const Vector3 forward = direction * (1.0f / distance);
    Vector3 bend = knee - hip;
    bend -= forward * bend.GetDotProduct(forward);
    if (bend.GetLength() <= 1e-5f) {
      bend = forward.GetCrossProduct(Vector3(0, 0, 1));
      if (bend.GetLength() <= 1e-5f)
        bend = forward.GetCrossProduct(Vector3(0, 1, 0));
    }
    bend = bend.GetNormalized(Vector3(0, 0, -1));
    const float along = (upper * upper - lower * lower + distance * distance) /
                        (2.0f * distance);
    const float height_sq = std::max(0.0f, upper * upper - along * along);
    const float height = std::sqrt(height_sq);
    result.knee = hip + forward * along + bend * height;
    result.ankle = goal;
    result.reachable = true;
    return result;
  }

  // Updates support anchors. A planted foot keeps its first world anchor until
  // support is lost or the pose leaves the ground band.
  static inline PlayerFootPlantState Update(
      const PlayerFootPlantState& previous, const PlayerFootPlantInput& input) {
    PlayerFootPlantState next = previous;
    const auto update = [&](bool support, const football::math::Vector3& ankle,
                            bool& planted, football::math::Vector3& anchor,
                            FootContactPhase& phase) {
      const bool in_band = ankle.coords[2] <= input.plant_height;
      if (!support || !in_band) {
        const bool was_planted = planted;
        planted = false;
        anchor = football::math::Vector3(0);
        phase = was_planted ? FootContactPhase::Releasing
                            : FootContactPhase::Swing;
        return;
      }
      if (!planted) {
        planted = true;
        anchor = ankle;
        phase = FootContactPhase::Planting;
        return;
      }
      phase = FootContactPhase::Planted;
    };
    update(input.left_support, input.left_ankle, next.left_planted,
           next.left_anchor, next.left_phase);
    update(input.right_support, input.right_ankle, next.right_planted,
           next.right_anchor, next.right_phase);
    return next;
  }
};

#endif  // FOOTBALL_SIM_PLAYER_FOOT_IK_HPP
