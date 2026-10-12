#include <catch2/catch_test_macros.hpp>
#include "model/pitch.hpp"

#include "football/ball/ball.hpp"

#include "football/pitch/pitch.hpp"

namespace {

using football::ball::Ball;
using football::ball::BallEnvironment;
using football::ball::BallState;
using blunted::Vector3;

const football::model::Pitch kPitch = football::model::Pitch{};
constexpr float kPostRadius = 0.06f;

BallState BehindGoalState() {
  BallState state;
  // Just behind the goal line but inside the goal mouth, so rear/side netting
  // is reachable regardless of the configured pitch dimensions.
  state.position = Vector3(kPitch.half_length() + kPitch.goal_depth(), 0.0f, 1.0f);
  state.velocity = Vector3(3.0f, 0.0f, 0.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  return state;
}

TEST_CASE("goal netting applies geometrically behind the goal line",
          "[sim][ball][contact]") {
  Ball ball{football::model::BallConfig{}, football::pitch::BuildBallWorld(kPitch, football::model::BallConfig{})};
  ball.Reset(BehindGoalState());
  const auto at_net = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  // The same moving state in midfield is not damped by any net geometry.
  BallState open = BehindGoalState();
  open.position = Vector3(0.0f, 0.0f, 1.0f);
  ball.Reset(open);
  const auto midfield = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  REQUIRE(midfield.velocity.coords[0] > 0.0f);
  // Rear-net absorption reduces the outgoing +x speed; this is now a pure
  // geometric fact, not a rule flag passed in BallEnvironment.
  REQUIRE(at_net.velocity.coords[0] < midfield.velocity.coords[0]);
}

TEST_CASE("woodwork contact reflects the ball off the post", "[sim][ball][contact]") {
  Ball ball{football::model::BallConfig{}, football::pitch::BuildBallWorld(kPitch, football::model::BallConfig{})};
  BallState state;
  // Place the ball just inside the post's collision radius, moving towards the
  // goal line so the first prediction step actually hits the post.
  state.position = Vector3(kPitch.half_length() - 0.15f, kPitch.goal_half_width(), 0.1f);
  state.velocity = Vector3(3.0f, 0.0f, 0.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  ball.Reset(state);

  const auto next = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});

  // The incoming +x velocity is reflected back out of the goal.
  REQUIRE(next.velocity.coords[0] < 0.0f);

  // After projection and integration the ball sits outside the combined
  // ball + post radius from the post centre.
  const Vector3 post_centre(kPitch.half_length(), kPitch.goal_half_width(), 0.0f);
  const float separation =
      (next.position.Get2D().GetAbsolute() - post_centre.Get2D().GetAbsolute()).GetLength();
  REQUIRE(separation >= 0.11f + kPostRadius - 0.001f);
}

}  // namespace
