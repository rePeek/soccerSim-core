#ifndef FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/player/command/player_motion_profile.hpp"
#include "football/player/state/player_kinematics.hpp"

struct MotionSample {
  football::math::Vector3 displacement = football::math::Vector3(0);
  football::math::Vector3 velocity = football::math::Vector3(0);
};

// A completed motion step. `state` is the proposed next kinematic state, so the
// committer does not have to re-integrate; `sample` stays readable for callers
// that only need the proposal.
struct MotionStep {
  PlayerKinematicState state;
  MotionSample sample;
};

// Continuous locomotion model. This is the public Command API: no Player,
// animation clip, contact or execution state is required.
class PlayerMotion {
 public:
  static MotionStep StepContinuous(const PlayerKinematicState& current,
                                   const PlayerMotionProfile& profile,
                                   const football::math::Vector3& desired_velocity,
                                   const football::math::Vector3& idle_facing,
                                   float dt);
  static MotionSample SampleContinuous(const PlayerKinematicState& current,
                                       const PlayerMotionProfile& profile,
                                       const football::math::Vector3& desired_velocity,
                                       const football::math::Vector3& idle_facing,
                                       float dt);
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
