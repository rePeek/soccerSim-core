#ifndef FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP

#include "football/animation/types.hpp"
#include "sim/player/player_body_profile.hpp"
#include "sim/player/player_kinematics.hpp"

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

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_LOCOMOTION_ANIMATION_HPP