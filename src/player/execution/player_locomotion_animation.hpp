#ifndef FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/reference_sample.hpp"
#include "football/animation/types.hpp"
#include "football/player/player_animation_style.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"

// LOCO-2.2: read-only interface between the off-ball motion authority and the
// animation adapter.
//
// Value snapshots for compatibility cadence/selection. They cannot mutate the
// Player. Root-driven execution uses PlayerActionAdapter and SampleAnimationStep;
// non-Movement actions retain their contact-frame and action-clock semantics.

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
// Compatibility playback derives clip time from achieved speed. The root-driven
// path instead samples paired root and pose through SampleAnimationStep.
// This helper returns only a cadence request; it does not commit displacement.
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
// intervals of travel. Dividing by frame_count would understate every clip's
// speed by (N-1)/N and bias the playback rate the wrong way.
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
// Import reports a reference stance speed for compatibility cadence.
// Playback reads the scalar fact, never the per-frame support analysis.
//
// Falls back to the root-track definition when the clip has no usable stance
// data (idle clips, actions, or a derivation that found nothing).
inline float LocomotionClipGaitSpeed(const AnimationClip& clip) {
  const float root_speed = LocomotionClipReferenceSpeed(clip);
  // An untrusted derivation must not drive cadence: a clip whose support phases
  // could not be read keeps the root-track definition.
  const float stance_speed =
      clip.motion_quality.support_trusted
          ? clip.motion_quality.stance_speed
          : 0.0f;
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

// Compatibility-only displacement: times are clamped within one cycle and
// end < start means one wrap. Keep the seam addition order for numerical parity.
// This cannot replace SampleAnimationStep's unwrapped, multi-cycle semantics.
// Root interpolation itself is shared with the paired Animation sampler.
inline football::math::Vector3 LocomotionReferenceDelta(
    const std::vector<football::math::Vector3>& roots, float start_time,
    float end_time) {
  if (roots.size() < 2) return football::math::Vector3(0);
  const float period = static_cast<float>(roots.size() - 1);
  if (end_time >= start_time) {
    return SampleRootPosition(roots, end_time) -
           SampleRootPosition(roots, start_time);
  }
  return (SampleRootPosition(roots, period) -
          SampleRootPosition(roots, start_time)) +
         (SampleRootPosition(roots, end_time) -
          SampleRootPosition(roots, 0.0f));
}

// The gait speed one player sees in a clip: the clip's own foot speed, scaled by
// his body. Both the stride target and the playback rate must use this one value;
// deriving it twice (once with the height scale, once without) biases the pairing
// for every player who is not exactly the reference build.
inline float LocomotionEffectiveGaitSpeed(const AnimationClip& clip,
                                          float height_scale) {
  return LocomotionClipGaitSpeed(clip) * height_scale;
}

// One tick of motion-driven playback. Pure: the same inputs always produce the
// same request, and the request carries no world position.
//
// `entry_frame_time` seeds the clip when it changes (the match entry frame);
// while the clip is unchanged the previous continuous time is carried over.
// `reference_speed` is the *effective* gait speed (already body-scaled); it is an
// explicit input so a caller cannot accidentally pair the stride against one
// speed and the rate against another. `stride_scale` shortens (or lengthens) the
// gait's fore-aft excursion; the playback rate divides by it so the support foot
// stays locked.
inline LocomotionPlaybackRequest AdvanceLocomotionPlayback(
    const LocomotionPlaybackRequest& previous, AnimationId clip_id,
    const AnimationClip& clip, float achieved_speed, float reference_speed,
    float entry_frame_time, float dt, float stride_scale = 1.0f) {
  LocomotionPlaybackRequest next;
  next.clip_id = clip_id;
  next.stride_scale = stride_scale;

  // A clip with no speed reference (idle, stance) has no cadence to derive from
  // the motion, so it keeps playing at its own rate - standing still must not
  // freeze an idle breathing cycle.
  const float effective_speed = reference_speed * stride_scale;
  if (effective_speed > 1e-3f) {
    next.playback_rate =
        achieved_speed <= kLocomotionStopSpeed
            ? 0.0f
            : std::clamp(achieved_speed / effective_speed, 0.0f,
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

// RM-F1: one animation phase for both the root-driven and the compatibility
// path. The wrapped playback time is what `PlaybackState` samples; the
// continuous time is what the next root sample starts from. Advancing only the
// former (the old fallback) used to leave the latter behind, so switching back
// to the root path jumped to a stale phase. The delta is wrap-aware and the
// clip change resets the cursor to the new clip's entry sample.
inline float AdvanceContinuousLocomotionTime(
    float continuous_frame, AnimationId previous_clip_id,
    AnimationId clip_id, float previous_frame_time, float next_frame_time,
    float period) {
  const float safe_period = period > 1e-3f ? period : 1.0f;
  if (previous_clip_id != clip_id) {
    // A new clip starts at its entry frame; earlier cycle counts belong to the
    // old clip and must not leak into the new timeline.
    return next_frame_time;
  }
  float delta = next_frame_time - previous_frame_time;
  if (delta < 0.0f) delta += safe_period;
  if (delta >= safe_period) delta = std::fmod(delta, safe_period);
  return std::max(continuous_frame + delta, 0.0f);
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
  // Reach-aware stride: the player's style asked for more than the legs can make
  // and the ceiling was applied before the cadence pairing.
  bool reach_limited = false;
  // RM-F3: the root sample was rejected by the turn-rate constraint this tick.
  bool turn_limited = false;
  // Where the residual comes from: a locked support anchor the leg cannot reach,
  // or a swinging foot whose own target is out of reach. They mean different
  // things and must not be conflated.
  float max_plant_residual = 0.0f;
  float max_swing_residual = 0.0f;
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP
