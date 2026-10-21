#include "referee/ball_rules.hpp"
#include "football/pitch/pitch.hpp"

#include <cmath>

#include "referee/goal.hpp"

namespace football::sim {
namespace {

// The parameter at which a linear segment crosses `bound` on `axis`, strictly
// inside the step. Null when the segment is parallel or does not reach it.
std::optional<float> CrossAxis(float previous, float current, float bound) {
  const float delta = current - previous;
  if (std::fabs(delta) < 1e-6f) return std::nullopt;
  const float t = (bound - previous) / delta;
  if (t <= 0.0f || t >= 1.0f) return std::nullopt;
  return t;
}

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

std::optional<football::math::Vector3> BoundaryCrossingPoint(
    const football::pitch::Pitch& pitch,
    const football::math::Vector3& previous,
    const football::math::Vector3& current, float ball_radius,
    BallOutOfPlay boundary) {
  if (boundary == BallOutOfPlay::None) return std::nullopt;
  const bool goal_line = boundary == BallOutOfPlay::GoalLine;
  const int axis = goal_line ? 0 : 1;
  const float limit = (goal_line ? pitch.half_length() : pitch.half_width()) +
                      pitch.line_half_width() + ball_radius;
  const float bound = limit * (current.coords[axis] < 0.0f ? -1.0f : 1.0f);
  const std::optional<float> t =
      CrossAxis(previous.coords[axis], current.coords[axis], bound);
  if (!t) return std::nullopt;
  return football::math::Vector3(previous + (current - previous) * *t);
}

}  // namespace football::sim
