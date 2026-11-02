#pragma once

#include <cassert>
#include <cmath>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::player {

using football::math::Vector3;

// Maps a velocity from the animation's own bucket range onto the player's top
// speed, preserving the walk/idle part.
inline float StretchSprintTo(const float &inputVelocity,
                             float inputSpaceMaxVelocity,
                             float targetMaxVelocity) {
  assert(targetMaxVelocity > walkSprintSwitch);

  if (inputVelocity < walkSprintSwitch) return inputVelocity;

  float howMuchSprintage = inputVelocity - walkSprintSwitch;

  float oldLength = inputSpaceMaxVelocity - walkSprintSwitch;
  float newLength = targetMaxVelocity - walkSprintSwitch;

  float toNewFactor = newLength / oldLength;

  float resultSprintage = howMuchSprintage * toNewFactor;

  return walkSprintSwitch + resultSprintage;
}

// Player selection preference: fast turns favour the incoming movement.
inline float CorneringBiasForFastCornering(const Vector3& current_movement,
                                           const Vector3& desired_movement,
                                           float velocity_power = 1.0f,
                                           float bias = 1.0f) {
  football::math::radian angle =
      desired_movement.GetNormalized(current_movement).GetAngle2D(current_movement);
  // wolfram alpha: sin(x - 0.5*pi) * 0.5 + 0.5, from x = 0 to pi.
  float current_movement_bias = std::sin(std::fabs(angle) - 0.5f * football::math::pi) * 0.5f + 0.5f;
  float velocity_bias = std::pow(
      football::math::clamp(current_movement.GetLength() / (sprintVelocity - 0.5f),
                            0.0f, 1.0f),
      velocity_power);
  return velocity_bias * current_movement_bias * bias;
}

}  // namespace football::player
