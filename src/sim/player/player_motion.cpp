#include "sim/player/player_motion.hpp"

#include <cmath>

PlayerLocomotionParameters PlayerMotion::LocomotionParameters(
    const PlayerMotionProfile& profile) {
  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = profile.max_speed;
  parameters.acceleration = profile.acceleration;
  parameters.braking = profile.braking;
  parameters.maxTurnRate = profile.turn_rate;
  return parameters;
}

MotionStep PlayerMotion::StepContinuous(
    const PlayerKinematicState& current, const PlayerMotionProfile& profile,
    const football::math::Vector3& desired_velocity,
    const football::math::Vector3& idle_facing, float dt) {
  MotionStep step;
  step.state = current;
  PlayerLocomotionInput input;
  input.desiredVelocity = desired_velocity;
  input.idleFacing = idle_facing;
  PlayerLocomotion::Step(step.state, input, LocomotionParameters(profile), dt);
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

football::math::Vector3 PlayerMotion::ClipDisplacement(const MotionClip& clip,
                                                       float frame) {
  if (clip.frames.empty() || frame <= 0.0f) return football::math::Vector3(0);
  const float last = static_cast<float>(clip.frames.size() - 1);
  const float clamped = std::fmin(frame, last);
  const auto lower = static_cast<std::size_t>(std::floor(clamped));
  const auto upper = static_cast<std::size_t>(std::ceil(clamped));
  const float fraction = clamped - static_cast<float>(lower);
  return clip.frames[lower].local_displacement * (1.0f - fraction) +
         clip.frames[upper].local_displacement * fraction;
}

MotionSample PlayerMotion::SampleClip(const MotionClip& clip, float frame,
                                      float dt) {
  MotionSample sample;
  sample.displacement = ClipDisplacement(clip, frame);
  if (dt > 0.0f) sample.velocity = sample.displacement * (1.0f / dt);
  return sample;
}
