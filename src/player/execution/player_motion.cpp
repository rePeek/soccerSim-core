#include "football/player/execution/player_motion.hpp"

#include "football/player/execution/player_motion_adapter.hpp"

MotionStep PlayerMotion::StepContinuous(
    const PlayerKinematicState& current, const PlayerMotionProfile& profile,
    const football::math::Vector3& desired_velocity,
    const football::math::Vector3& idle_facing, float dt) {
  // P4: PlayerMotion is now a compatibility facade over the single motion
  // constraint core. Locomotion mode consumes the control target directly; the
  // reference delta is only used to report whether the target was reproduced.
  PlayerMotionConstraintInput input;
  input.reference_delta = desired_velocity * dt;
  input.current = current;
  input.body = football::player::PlayerBodyProfile{};
  input.capability = profile;
  input.desired_velocity = desired_velocity;
  input.authority = PlayerMotionAuthority::Locomotion;
  input.dt = dt;
  input.idle_facing = idle_facing;
  const PlayerMotionConstraintResult result = PlayerMotionAdapter::Step(input);

  MotionStep step;
  step.state = result.state;
  step.sample.displacement = result.achieved_delta;
  step.sample.velocity = result.state.velocity;
  return step;
}

MotionSample PlayerMotion::SampleContinuous(
    const PlayerKinematicState& current, const PlayerMotionProfile& profile,
    const football::math::Vector3& desired_velocity,
    const football::math::Vector3& idle_facing, float dt) {
  return StepContinuous(current, profile, desired_velocity, idle_facing, dt)
      .sample;
}
