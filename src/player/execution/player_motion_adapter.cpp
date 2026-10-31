#include "football/player/execution/player_motion_adapter.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "player/execution/player_locomotion.hpp"

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

football::math::Vector3 ResolveIdleFacing(
    const PlayerMotionConstraintInput& input) {
  const football::math::Vector3 idle = input.idle_facing.Get2D();
  if (idle.GetLength() > 1e-6f) return idle.GetNormalized();
  return input.current.GetFacing();
}

}  // namespace

PlayerMotionConstraintResult PlayerMotionAdapter::Step(
    const PlayerMotionConstraintInput& input) {
  assert(input.dt > 0.0f);
  assert(input.capability.max_speed > 0.0f);

  const football::math::Vector3 scaled_reference =
      input.reference_delta * input.body.height_scale;

  // The reference is the trajectory for one-shot actions. Locomotion uses the
  // control target; physical reactions use the physics-provided velocity.
  football::math::Vector3 target_velocity = input.desired_velocity;
  if (input.authority == PlayerMotionAuthority::ReferenceAction) {
    const float reference_speed = scaled_reference.GetLength() / input.dt;
    if (reference_speed > 1e-6f) {
      float speed_cap = reference_speed;
      const float desired_speed = input.desired_velocity.GetLength();
      if (desired_speed > 1e-6f) speed_cap = std::min(speed_cap, desired_speed);
      speed_cap = std::min(speed_cap, input.capability.max_speed);
      target_velocity =
          scaled_reference.GetNormalized(0) * speed_cap;
    } else {
      target_velocity = football::math::Vector3(0);
    }
  } else if (input.authority == PlayerMotionAuthority::Locomotion) {
    // Keep the control target untouched: PlayerLocomotion is the one place
    // that clamps speed and applies the turn penalty, exactly as before P4.
  }

  PlayerKinematicState next = input.current;
  PlayerLocomotionInput locomotion_input;
  locomotion_input.desiredVelocity = target_velocity;
  locomotion_input.idleFacing = ResolveIdleFacing(input);
  PlayerLocomotion::Step(next, locomotion_input,
                         ToLocomotionParameters(input.capability), input.dt);

  PlayerMotionConstraintResult result;
  result.state = next;
  result.achieved_delta = next.position - input.current.position;
  // A reference action is constrained when the committed displacement differs
  // from the reference it was asked to follow. Locomotion may legitimately
  // differ; the flag still reports whether the reference was reproduced.
  result.constrained =
      (result.achieved_delta - scaled_reference).GetLength() > 1e-4f;
  return result;
}
