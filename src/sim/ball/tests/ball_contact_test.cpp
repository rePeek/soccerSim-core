#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball.hpp"

namespace {

using football::ball::Ball;
using football::ball::BallEnvironment;
using football::ball::BallState;
using blunted::Vector3;

const football::model::Pitch kPitch = football::model::Pitch{};

BallState BehindGoalState() {
  BallState state;
  state.position = Vector3(58.5f, 0.0f, 1.0f);
  state.velocity = Vector3(3.0f, 0.0f, 0.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  return state;
}

TEST_CASE("goal netting changes trajectory only when the rule fact is set",
          "[sim][ball][contact]") {
  Ball ball(kPitch);
  ball.Reset(BehindGoalState());

  const auto outside = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  const auto inside = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{true});

  REQUIRE(inside.velocity != outside.velocity);
  REQUIRE(inside.velocity.coords[0] != outside.velocity.coords[0]);
}

TEST_CASE("woodwork contact keeps the ball inside the goal bounds",
          "[sim][ball][contact]") {
  Ball ball(kPitch);
  BallState state;
  state.position = Vector3(55.0f, 3.7f, 0.1f);
  state.velocity = Vector3(1.0f, 0.0f, 0.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  ball.Reset(state);

  const auto next = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  // The post projection must not push the ball past the goal line.
  REQUIRE(next.position.coords[0] <= 55.0f + 0.11f + 0.07f + 0.01f);
}

}  // namespace