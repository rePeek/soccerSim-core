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

BallBoundaryCrossing DetectBallBoundaryCrossing(
    const football::pitch::Pitch& pitch,
    const football::math::Vector3& previous,
    const football::math::Vector3& current, float ball_radius) {
  BallBoundaryCrossing result;
  const float limit_x = pitch.half_length() + pitch.line_half_width() + ball_radius;
  const float limit_y = pitch.half_width() + pitch.line_half_width() + ball_radius;
  float best = 2.0f;
  for (int sign : {-1, 1}) {
    if (const auto t = CrossAxis(previous.coords[0], current.coords[0],
                                 static_cast<float>(sign) * limit_x);
        t && *t < best) {
      best = *t;
      result.boundary = BallOutOfPlay::GoalLine;
    }
    if (const auto t = CrossAxis(previous.coords[1], current.coords[1],
                                 static_cast<float>(sign) * limit_y);
        t && *t < best) {
      best = *t;
      result.boundary = BallOutOfPlay::Touchline;
    }
  }
  if (result.boundary != BallOutOfPlay::None) {
    result.fraction = best;
    result.position = previous + (current - previous) * best;
    return result;
  }
  // Already outside at the start of the step: keep the line it was out on.
  const BallOutOfPlay at_previous =
      ClassifyBallOutOfPlay(pitch, previous, ball_radius);
  result.boundary = at_previous != BallOutOfPlay::None
                        ? at_previous
                        : ClassifyBallOutOfPlay(pitch, current, ball_radius);
  result.position = at_previous != BallOutOfPlay::None ? previous : current;
  result.fraction = at_previous != BallOutOfPlay::None ? 0.0f : 1.0f;
  return result;
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
