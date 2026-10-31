#ifndef FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/player/player_animation_style.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"

// LOCO-2.2: read-only interface between the off-ball motion authority and the
// animation adapter.
//
// The boundary is the whole point: PlayerMotion decides how far the player
// moves; the animation adapter only chooses which pose performs that motion.
// This input is a value snapshot, never a reference into live state, so the
// adapter cannot mutate the Player. The output is a *proposal* of animation
// identity/timing only -- it never contains a world position and never writes
// PlayerKinematicState. Non-Movement actions keep their existing contact-frame
// and action-clock semantics and are not described here.

struct LocomotionAnimationInput {
  // Kinematic state before and after the tick under consideration. `next` is
  // the proposal the Player is about to commit (or already committed), so the
  // adapter sees the same velocity/turn the physics produced.
  PlayerKinematicState current;
  PlayerKinematicState next;
  // The requested velocity for this tick (before rate/turn limits apply).
  football::math::Vector3 desired_velocity;
  // Body dimensions shared with the collision geometry (BODY-1 authority).
  football::player::PlayerBodyProfile body;
};

struct LocomotionAnimationOutput {
  // Selected clip. -1 means "no proposal"; the caller keeps its current clip.
  AnimationId animation_id = -1;
  // Playback multiplier relative to the clip's reference speed. 1.0 is native.
  float playback_rate = 1.0f;
  // Continuous clip time in frames the adapter wants to sample (LOCO-2.6).
  float frame_time = 0.0f;
  // Minimal gait bookkeeping: support/transition phase and a switch request.
  // Deliberately not a state machine; extended only when measured to be needed.
  int gait_phase = 0;
  bool suggest_switch = false;
};

inline LocomotionAnimationInput MakeLocomotionAnimationInput(
    const PlayerKinematicState& current, const PlayerKinematicState& next,
    const football::math::Vector3& desired_velocity,
    const football::player::PlayerBodyProfile& body) {
  return LocomotionAnimationInput{current, next, desired_velocity, body};
}

// ---------------------------------------------------------------------------
// M2/STRIDE: motion-driven locomotion playback.
//
// Ordinary locomotion is interpolated on the motion the Player actually
// achieved. The action clock still owns action time; for a Movement clip there
// is no contact event to protect, so clip time is derived from the achieved
// speed relative to the clip's reference speed:
//
//   rate      = achieved_speed / (clip_reference_speed * stride_scale)
//   clip_time += rate * dt / clip_frame_duration
//
// The clip time is continuous and wraps at the end of the clip, so the gait
// phase never restarts mid-stride just because a tick boundary was crossed.
// Root XY of the clip is never read here: it can change the gait, never the
// Player's world trajectory.
// ---------------------------------------------------------------------------
struct LocomotionPlaybackRequest {
  AnimationId clip_id = -1;
  // Continuous clip frame to sample this tick.
  float frame_time = 0.0f;
  // Achieved speed / effective reference speed, bounded above only. 0 means the
  // Player is standing and a movement clip must not advance.
  float playback_rate = 1.0f;
  // Position within the clip cycle, 0..1. Continuous across ticks and wraps.
  float gait_phase = 0.0f;
  // STRIDE-1: the stride scale this rate was computed against.
  float stride_scale = 1.0f;
  bool looped = false;
};

// One frame of clip time is 10 ms at playback rate 1 (the bake cadence).
inline constexpr float kLocomotionClipFrame_ms = 10.0f;
// A stride may be quickened, but not arbitrarily: beyond this the clip no longer
// reads as the same gait. There is deliberately no *lower* bound: the rate is
// derived from the achieved speed, and flooring it would make the stance foot
// travel faster than the root (which the plant would then have to absorb).
// A stationary Player freezes a movement clip; an idle clip has no speed
// reference and keeps its own cadence.
inline constexpr float kLocomotionMaxPlaybackRate = 2.2f;
// Below this the Player is treated as standing: a movement clip stops advancing.
inline constexpr float kLocomotionStopSpeed = 0.05f;

// STRIDE-1: how much of a speed mismatch is absorbed by stride length instead
// of cadence. 0 = cadence only (the reference plays in slow motion), 1 = stride
// only (native cadence with shorter steps). The value in between is a blend.
//
// Both extremes keep the support foot locked in the world, because the foot
// excursion and the playback rate are scaled by the same factor: a stance foot
// moves `stride_scale * reference_speed * playback_rate` backwards in clip
// space, and `playback_rate = achieved / (reference * stride_scale)` makes that
// exactly `achieved`. Changing one without the other is what causes foot slide.
inline constexpr float kLocomotionStrideCoupling = 0.5f;
inline constexpr float kLocomotionMinStrideScale = 0.6f;
inline constexpr float kLocomotionMaxStrideScale = 1.35f;
// Stride changes are a per-gait-cycle adaptation, not a per-tick reaction: an
// acceleration spike must not produce a one-tick leg-length change.
inline constexpr float kLocomotionStrideTimeConstant_s = 0.3f;

// Mean reference speed of a clip from its baked root trajectory. 0 when the
// clip carries no usable trajectory, which means "no speed reference".
//
// `root_positions` holds one sample per clip frame, so N samples span N-1 tick
// intervals of travel. Dividing by frame_count would overstate every clip's
// speed by N/(N-1) and bias the playback rate the wrong way.
inline float LocomotionClipReferenceSpeed(const AnimationClip& clip) {
  const std::size_t samples = clip.root_positions.size();
  if (samples < 2) return 0.0f;
  const auto d = clip.root_positions.back() - clip.root_positions.front();
  const float dist = std::sqrt(d.coords[0] * d.coords[0] + d.coords[1] * d.coords[1]);
  const float span_s = (static_cast<float>(samples - 1) * kLocomotionClipFrame_ms) / 1000.0f;
  if (span_s <= 0.0f) return 0.0f;
  return dist / span_s;
}

// The speed the *feet* imply, which is what playback must follow.
//
// Authored clips carry foot slide: on the shipped catalog a run clip's stance
// foot travels ~3.5 m/s inside the animation while its root track claims ~5 m/s.
// Playing such a clip at `achieved / root_track_speed` therefore drags the stance
// foot across the ground for the whole stance, and the plant has to absorb it.
// The derived support track (`clip.stance_speed`, see foot_support.hpp) states how
// fast the stance foot actually travels, so this is the speed a foot-locked
// playback should use.
//
// Falls back to the root-track definition when the clip has no usable stance
// data (idle clips, actions, or a derivation that found nothing).
inline float LocomotionClipGaitSpeed(const AnimationClip& clip) {
  const float root_speed = LocomotionClipReferenceSpeed(clip);
  const float stance_speed = clip.stance_speed;
  if (stance_speed <= 0.5f) return root_speed;
  // A stance foot cannot travel much faster than the body it carries; anything
  // beyond that is derivation noise, not a cadence reference.
  if (root_speed > 1e-3f && stance_speed > 1.5f * root_speed + 1.0f) {
    return root_speed;
  }
  return stance_speed;
}

// The stride scale the achieved/reference speed mismatch asks for.
//
// Neutral style (stride 1, cadence 1, response kLocomotionStrideCoupling) gives
// exactly `(achieved / reference) ^ kLocomotionStrideCoupling`, which is the
// behaviour this pipeline shipped before per-player styles existed.
inline float LocomotionStrideScaleTarget(float achieved_speed, float reference_speed) {
  if (reference_speed <= 1e-3f || achieved_speed <= 0.0f) return 1.0f;
  const float ratio = achieved_speed / reference_speed;
  return std::clamp(std::pow(ratio, kLocomotionStrideCoupling),
                    kLocomotionMinStrideScale, kLocomotionMaxStrideScale);
}

// A player's stride/cadence preference, as the playback needs it.
struct LocomotionStrideStyle {
  float stride_preference = 1.0f;
  float cadence_preference = 1.0f;
  float stride_response = kLocomotionStrideCoupling;
};

// Personal stride target.
//
//   q    = A / (R * S0 * C0)      how far the motion sits from this player's
//                                 preferred gait speed for this clip
//   S    = clamp(S0 * q^lambda)   share `lambda` of the difference goes to the
//                                 step length, the rest to cadence
//   rate = A / (R * S)            derived, never chosen independently
//
// At the preferred speed (A = R * S0 * C0) this returns exactly S0, and the
// playback rate comes out as C0: the two preferences mean what they say.
inline float LocomotionStrideScaleFor(const LocomotionStrideStyle& style,
                                      float achieved_speed,
                                      float reference_speed) {
  const float stride_preference =
      std::clamp(style.stride_preference, kAnimationStrideMin, kAnimationStrideMax);
  const float cadence_preference =
      std::clamp(style.cadence_preference, kAnimationCadenceMin, kAnimationCadenceMax);
  const float response =
      std::clamp(style.stride_response, 0.0f, 1.0f);
  if (reference_speed <= 1e-3f || achieved_speed <= 0.0f) {
    return std::clamp(stride_preference, kLocomotionMinStrideScale,
                      kLocomotionMaxStrideScale);
  }
  const float preferred_speed = reference_speed * stride_preference * cadence_preference;
  if (preferred_speed <= 1e-6f) {
    return std::clamp(stride_preference, kLocomotionMinStrideScale,
                      kLocomotionMaxStrideScale);
  }
  const float ratio = achieved_speed / preferred_speed;
  return std::clamp(stride_preference * std::pow(ratio, response),
                    kLocomotionMinStrideScale, kLocomotionMaxStrideScale);
}

// Exponential smoothing of the stride scale. Deterministic, one call per tick.
inline float SmoothLocomotionStrideScale(float current, float target, float dt) {
  if (kLocomotionStrideTimeConstant_s <= 0.0f) return target;
  const float alpha = 1.0f - std::exp(-dt / kLocomotionStrideTimeConstant_s);
  return current + (target - current) * alpha;
}

// Cumulative root reference position at a fractional frame time, measured from
// the clip's first sample. Interpolated between baked samples and clamped at
// the ends, so a caller never re-derives the trajectory itself.
inline football::math::Vector3 LocomotionRootPositionAt(
    const std::vector<football::math::Vector3>& roots, float frame_time) {
  if (roots.empty()) return football::math::Vector3(0);
  const float last = static_cast<float>(roots.size() - 1);
  const float t = std::clamp(frame_time, 0.0f, last);
  const auto lower = static_cast<std::size_t>(std::floor(t));
  const float fraction = t - static_cast<float>(lower);
  football::math::Vector3 position = roots[lower] - roots.front();
  if (fraction > 0.0f && lower + 1 < roots.size()) {
    position += (roots[lower + 1] - roots[lower]) * fraction;
  }
  return position;
}

// Reference displacement between two clip times of the *same* cycle. A wrap
// (end < start) is measured across the loop boundary, so the reported reference
// distance of a tick never collapses when the cycle restarts.
inline football::math::Vector3 LocomotionReferenceDelta(
    const std::vector<football::math::Vector3>& roots, float start_time,
    float end_time) {
  if (roots.size() < 2) return football::math::Vector3(0);
  const float period = static_cast<float>(roots.size() - 1);
  if (end_time >= start_time) {
    return LocomotionRootPositionAt(roots, end_time) -
           LocomotionRootPositionAt(roots, start_time);
  }
  return (LocomotionRootPositionAt(roots, period) -
          LocomotionRootPositionAt(roots, start_time)) +
         (LocomotionRootPositionAt(roots, end_time) -
          LocomotionRootPositionAt(roots, 0.0f));
}

// One tick of motion-driven playback. Pure: the same inputs always produce the
// same request, and the request carries no world position.
//
// `entry_frame_time` seeds the clip when it changes (the match entry frame);
// while the clip is unchanged the previous continuous time is carried over.
// `stride_scale` shortens (or lengthens) the gait's fore-aft excursion; the
// playback rate divides by it so the support foot stays locked.
inline LocomotionPlaybackRequest AdvanceLocomotionPlayback(
    const LocomotionPlaybackRequest& previous, AnimationId clip_id,
    const AnimationClip& clip, float achieved_speed, float entry_frame_time,
    float dt, float stride_scale = 1.0f) {
  LocomotionPlaybackRequest next;
  next.clip_id = clip_id;
  next.stride_scale = stride_scale;

  // A clip with no speed reference (idle, stance) has no cadence to derive from
  // the motion, so it keeps playing at its own rate - standing still must not
  // freeze an idle breathing cycle.
  const float reference_speed = LocomotionClipGaitSpeed(clip) * stride_scale;
  if (reference_speed > 1e-3f) {
    next.playback_rate =
        achieved_speed <= kLocomotionStopSpeed
            ? 0.0f
            : std::clamp(achieved_speed / reference_speed, 0.0f,
                         kLocomotionMaxPlaybackRate);
  }

  const float frame_count = static_cast<float>(std::max(clip.frame_count, 1u));
  // The cycle length: frame 0 and the last frame are the same gait instant in a
  // looping movement clip, so the wrap period is frame_count - 1.
  const float period = frame_count > 1.0f ? frame_count - 1.0f : 1.0f;

  float time = previous.clip_id == clip_id ? previous.frame_time : entry_frame_time;
  time += next.playback_rate * (dt * 1000.0f / kLocomotionClipFrame_ms);

  if (time >= period) {
    time = std::fmod(time, period);
    next.looped = true;
  }
  if (time < 0.0f) time = 0.0f;

  next.frame_time = time;
  next.gait_phase = time / period;
  return next;
}

// STRIDE-0: measured locomotion/animation agreement for one tick. Diagnostic
// only - nothing in production branches on these values. `reference_distance`
// is measured over the frames that were actually sampled this tick (wrap
// aware), never over the clip's nominal per-tick travel.
struct LocomotionPoseDiagnostics {
  float achieved_distance = 0.0f;
  float reference_distance = 0.0f;
  float playback_rate = 1.0f;
  float stride_scale = 1.0f;
  // STRIDE-0 baseline: how far the raw animation ankle sits from the support
  // anchor, i.e. the slide the clip itself would produce. This is the number
  // stride/cadence pairing is supposed to drive down.
  float left_animation_slip = 0.0f;
  float right_animation_slip = 0.0f;
  // What actually ships: the residual of the settled final pose against the
  // anchor. Non-zero only when the plant target was outside the leg's reach.
  float left_foot_slip = 0.0f;
  float right_foot_slip = 0.0f;
  // P1: an anchor the leg could not reach was given up / slid this tick.
  bool left_plant_released = false;
  bool right_plant_released = false;
  float left_plant_slide = 0.0f;
  float right_plant_slide = 0.0f;
  // P1: peak leg-joint rotation between the previous and this Final Pose, in
  // radians per tick. The animation realization budgets the incoming pose, not
  // the plant solve that follows it, so this is the measured truth.
  float max_leg_joint_step = 0.0f;
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP
