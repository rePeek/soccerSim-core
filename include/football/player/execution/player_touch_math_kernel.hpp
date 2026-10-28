#ifndef FOOTBALL_PLAYER_TOUCH_MATH_KERNEL_HPP
#define FOOTBALL_PLAYER_TOUCH_MATH_KERNEL_HPP

#include "football/foundation/math/vector3.hpp"

namespace football::sim::player {

struct TouchDifficultyInput {
  football::math::Vector3 position_offset;
  football::math::Vector3 ball_position_2d;
  football::math::Vector3 ball_velocity;
  football::math::Vector3 spatial_movement;
  football::math::Vector3 spatial_position;
  football::math::Vector3 spatial_direction;
  float technical_ballcontrol = 0.0f;
  // Resolved by Execution from the opponent's last-touch record; 0 means none.
  float last_touch_bias = 0.0f;
};

struct TouchDifficultyFactors {
  float distance = 0.0f;
  float height = 0.0f;
  float ball_movement = 0.0f;
};

TouchDifficultyFactors CalculateTouchDifficulty(
    const TouchDifficultyInput& input);

}  // namespace football::sim::player

#endif  // FOOTBALL_PLAYER_TOUCH_MATH_KERNEL_HPP
