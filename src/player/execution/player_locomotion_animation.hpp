#ifndef FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP

#include <algorithm>
#include <cmath>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
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
// M2: motion-driven locomotion playback.
//
// Ordinary locomotion is interpolated on the motion the Player actually
// achieved. The action clock still owns action time; for a Movement clip there
// is no contact event to protect, so clip time is derived from the achieved
// speed relative to the clip's reference speed:
//
//   rate      = achieved_speed / clip_reference_speed   (bounded)
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
  // Achieved speed / clip reference speed, clamped to a plausible range.
  float playback_rate = 1.0f;
  // Position within the clip cycle, 0..1. Continuous across ticks and wraps.
  float gait_phase = 0.0f;
  bool looped = false;
};

// One frame of clip time is 10 ms at playback rate 1 (the bake cadence).
inline constexpr float kLocomotionClipFrame_ms = 10.0f;
// A stride may be slowed or quickened, but not arbitrarily: outside this range
// the clip no longer reads as the same gait.
inline constexpr float kLocomotionMinPlaybackRate = 0.35f;
inline constexpr float kLocomotionMaxPlaybackRate = 2.2f;

// Mean reference speed of a clip from its baked root trajectory. 0 when the
// clip carries no usable trajectory, which means "no speed reference".
inline float LocomotionClipReferenceSpeed(const AnimationClip& clip) {
  if (clip.root_positions.size() < 2 || clip.frame_count == 0) return 0.0f;
  const auto d = clip.root_positions.back() - clip.root_positions.front();
  const float dist = std::sqrt(d.coords[0] * d.coords[0] + d.coords[1] * d.coords[1]);
  return dist / (static_cast<float>(clip.frame_count) / 100.0f);
}

// One tick of motion-driven playback. Pure: the same inputs always produce the
// same request, and the request carries no world position.
//
// `entry_frame_time` seeds the clip when it changes (the match entry frame);
// while the clip is unchanged the previous continuous time is carried over.
inline LocomotionPlaybackRequest AdvanceLocomotionPlayback(
    const LocomotionPlaybackRequest& previous, AnimationId clip_id,
    const AnimationClip& clip, float achieved_speed,
    float entry_frame_time, float dt) {
  LocomotionPlaybackRequest next;
  next.clip_id = clip_id;

  const float reference_speed = LocomotionClipReferenceSpeed(clip);
  if (reference_speed > 1e-3f && achieved_speed > 0.0f) {
    next.playback_rate = std::clamp(achieved_speed / reference_speed,
                                    kLocomotionMinPlaybackRate,
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

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP
