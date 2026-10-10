#include "sim/referee/ball_rules.hpp"

#include <cmath>

#include "sim/referee/goal.hpp"

namespace football::sim {
namespace {

// Legacy out-of-play grace margin, preserved exactly.
constexpr float kOutOfPlayMargin = 0.11f;

}  // namespace

BallOutOfPlay ClassifyBallOutOfPlay(const football::model::Pitch& pitch,
                                    const blunted::Vector3& ball_position) {
  // Goal-line priority is preserved: an instant is at most one condition, and
  // the goal line is tested before the touchline.
  const float limit_x = pitch.half_length() + pitch.line_half_width() + kOutOfPlayMargin;
  if (std::fabs(ball_position.coords[0]) > limit_x) return BallOutOfPlay::GoalLine;
  const float limit_y = pitch.half_width() + pitch.line_half_width() + kOutOfPlayMargin;
  if (std::fabs(ball_position.coords[1]) > limit_y) return BallOutOfPlay::Touchline;
  return BallOutOfPlay::None;
}

std::optional<int> CrossedGoalMouth(const football::model::Pitch& pitch,
                                    const blunted::Vector3& previous,
                                    const blunted::Vector3& current) {
  for (int side : {-1, 1}) {
    if (CrossedGoalLine(pitch, side, previous, current)) return side;
  }
  return std::nullopt;
}

}  // namespace football::sim
