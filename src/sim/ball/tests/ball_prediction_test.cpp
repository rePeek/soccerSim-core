#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball.hpp"

namespace {

using football::ball::Ball;
using football::ball::BallEnvironment;
using football::ball::BallState;
using blunted::Vector3;

const football::model::Pitch kPitch = football::model::MakeLegacyPitch();

BallState InitialState() {
  BallState state;
  state.position = Vector3(0.0f, 0.0f, 0.4f);
  state.velocity = Vector3(6.0f, 2.0f, 4.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  return state;
}

TEST_CASE("one-tick prediction equals one-tick step", "[sim][ball][prediction]") {
  Ball predicted(kPitch);
  Ball stepped(kPitch);
  predicted.Reset(InitialState());
  stepped.Reset(InitialState());

  const auto ahead = predicted.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  stepped.Step(football::sim::TickSpan{1}, BallEnvironment{});

  REQUIRE(ahead.position == stepped.state().position);
  REQUIRE(ahead.velocity == stepped.state().velocity);
  REQUIRE(ahead.angular_velocity == stepped.state().angular_velocity);
}

TEST_CASE("prediction is read-only and matches a re-integrated step",
          "[sim][ball][prediction]") {
  Ball ball(kPitch);
  ball.Reset(InitialState());
  const auto before = ball.state();

  const auto ahead = ball.Predict(football::sim::TickSpan{4}, BallEnvironment{});
  REQUIRE(ball.state().position == before.position);
  REQUIRE(ball.state().velocity == before.velocity);

  Ball replayed(kPitch);
  replayed.Reset(InitialState());
  for (int i = 0; i < 4; ++i) {
    replayed.Step(football::sim::TickSpan{1}, BallEnvironment{});
  }
  REQUIRE(ahead.position == replayed.state().position);
  REQUIRE(ahead.velocity == replayed.state().velocity);
}

}  // namespace