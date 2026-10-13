#ifndef FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP

#include "football/animation/motion_clip.hpp"
#include "football/foundation/math/vector3.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_motion_profile.hpp"

// A tick's motion proposal, before any collision or authority resolution. It is
// what a motion source *asks* for; Player owns the resulting state.
struct MotionSample {
  football::math::Vector3 displacement = football::math::Vector3(0);
  football::math::Vector3 velocity = football::math::Vector3(0);
};

// The single motion interface, with two sources: the continuous locomotion model
// and a baked MotionClip reference.
//
// Exactly one place commits the result (Player). That is what makes an animation
// change unable to move the player by itself, and it is the interface action
// execution will later call instead of writing Humanoid's spatial state.
class PlayerMotion {
 public:
  // Continuous model. A facade over PlayerLocomotion, not a second
  // implementation: it steps a copy of the state and reports the delta.
  static MotionSample SampleContinuous(const PlayerKinematicState& current,
                                       const PlayerMotionProfile& profile,
                                       const football::math::Vector3& desired_velocity,
                                       const football::math::Vector3& idle_facing,
                                       float dt);

  // Baked reference. `frame` is a fractional clip frame, so a clip playing at a
  // non-unit rate is sampled without touching the clip.
  static football::math::Vector3 ClipDisplacement(const MotionClip& clip,
                                                  float frame);
  static MotionSample SampleClip(const MotionClip& clip, float frame, float dt);

  // Maps the four capability fields onto the locomotion parameter set. The
  // remaining locomotion parameters keep their defaults until the motion
  // authority move (P2) needs more of them.
  static PlayerLocomotionParameters LocomotionParameters(
      const PlayerMotionProfile& profile);
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
