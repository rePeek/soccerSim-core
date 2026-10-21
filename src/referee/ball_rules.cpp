#include "referee/ball_rules.hpp"
#include "football/pitch/pitch.hpp"

#include <cmath>

#include "referee/goal.hpp"

namespace football::sim {
namespace {

// Legacy out-of-play grace margin, preserved exactly.
constexpr float kOutOfPlayMargin = 0.11f;

}  // namespace

BallOutOfPlay ClassifyBallOutOfPlay(const football::pitch::Pitch& pitch,
                                    const football::math::Vector3& ball_position,
                                    float ball_radius) {
  // Goal-line priority is preserved: an instant is at most one condition, and
  // the goal line is tested before the touchline. The whole ball must be past
  // the line, so the boundary is offset by one ball radius.
  const float limit_x = pitch.half_length() + pitch.line_half_width() + ball_radius;
  if (std::fabs(ball_position.coords[0]) > limit_x) return BallOutOfPlay::GoalLine;
  const float limit_y = pitch.half_width() + pitch.line_half_width() + ball_radius;
  if (std::fabs(ball_position.coords[1]) > limit_y) return BallOutOfPlay::Touchline;
  return BallOutOfPlay::None;
}

std::optional<int> CrossedGoalMouth(const football::pitch::Pitch& pitch,
                                    const football::math::Vector3& previous,
                                    const football::math::Vector3& current,
                                    float ball_radius) {
  for (int side : {-1, 1}) {
    if (CrossedGoalLine(pitch, side, previous, current, ball_radius)) return side;
  }
  return std::nullopt;
}

}  // namespace football::sim
