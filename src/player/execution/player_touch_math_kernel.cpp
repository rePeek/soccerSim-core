#include "football/player/execution/player_touch_math_kernel.hpp"

#include <algorithm>
#include <cmath>

#include "football/foundation/math/scalar.hpp"

namespace football::sim::player {

TouchDifficultyFactors CalculateTouchDifficulty(
    const TouchDifficultyInput& input) {
  using football::math::clamp;
  using football::math::NormalizedClamp;

  float distance_factor = 0.0f;
  float height_factor = 0.0f;
  float ball_movement_factor = 0.0f;

  const float position_offset_penalty =
      NormalizedClamp(input.position_offset.GetLength(), 0.0f, 0.1f) * 2.0f;
  const float ball_body_velocity_penalty =
      std::pow(NormalizedClamp(
                   (input.spatial_movement - input.ball_velocity).GetLength(),
                   10.0f, 50.0f),
               1.5f) *
      5.0f;
  const float farther_away_penalty =
      std::pow(NormalizedClamp(
                   ((input.spatial_position +
                     input.spatial_direction * 0.2f) -
                    input.ball_position_2d)
                       .GetLength(),
                   0.7f, 1.3f),
               2.0f) *
      2.0f;

  distance_factor += position_offset_penalty * 2.0f;
  distance_factor += ball_body_velocity_penalty;
  distance_factor += farther_away_penalty * 4.0f;

  height_factor += position_offset_penalty * 0.5f;
  height_factor += ball_body_velocity_penalty * 2.0f;
  height_factor += farther_away_penalty;

  if (input.last_touch_bias > 0.0f) {
    const float last_touch_penalty =
        std::pow(input.last_touch_bias, 0.6f) * 5.0f;
    distance_factor += last_touch_penalty;
    height_factor += last_touch_penalty;
    ball_movement_factor += last_touch_penalty * 0.1f;
  }
  ball_movement_factor = clamp(ball_movement_factor, 0.0f, 0.9f);

  const float skill_penalty_multiplier =
      1.0f - input.technical_ballcontrol * 0.5f;
  return TouchDifficultyFactors{
      clamp(distance_factor * skill_penalty_multiplier, 0.0f, 1.0f),
      clamp(height_factor * skill_penalty_multiplier, 0.0f, 1.0f),
      clamp(ball_movement_factor * skill_penalty_multiplier, 0.0f, 1.0f)};
}

}  // namespace football::sim::player
