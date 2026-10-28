#include "football/player/command/player_motion.hpp"

#include "player/command/player_locomotion.hpp"

namespace {
PlayerLocomotionParameters ToLocomotionParameters(
    const PlayerMotionProfile& profile) {
  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = profile.max_speed;
  parameters.acceleration = profile.acceleration;
  parameters.braking = profile.braking;
  parameters.maxTurnRate = profile.turn_rate;
  return parameters;
}
}  // namespace

MotionStep PlayerMotion::StepContinuous(
    const PlayerKinematicState& current, const PlayerMotionProfile& profile,
    const football::math::Vector3& desired_velocity,
    const football::math::Vector3& idle_facing, float dt) {
  MotionStep step;
  step.state = current;
  PlayerLocomotionInput input;
  input.desiredVelocity = desired_velocity;
  input.idleFacing = idle_facing;
  PlayerLocomotion::Step(step.state, input, ToLocomotionParameters(profile), dt);
  step.sample.displacement = step.state.position - current.position;
  step.sample.velocity = step.state.velocity;
  return step;
}

MotionSample PlayerMotion::SampleContinuous(
    const PlayerKinematicState& current, const PlayerMotionProfile& profile,
    const football::math::Vector3& desired_velocity,
    const football::math::Vector3& idle_facing, float dt) {
  return StepContinuous(current, profile, desired_velocity, idle_facing, dt)
      .sample;
}
