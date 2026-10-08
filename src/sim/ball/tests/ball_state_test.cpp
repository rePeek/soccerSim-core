#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball.hpp"

namespace {

using football::ball::Ball;
using football::ball::BallConfig;
using football::ball::BallState;
using blunted::Vector3;

const football::model::Pitch kPitch = football::model::MakeLegacyPitch();

BallState TestState() {
  BallState state;
  state.position = Vector3(1.0f, 2.0f, 0.5f);
  state.velocity = Vector3(3.0f, -1.0f, 0.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  return state;
}

TEST_CASE("Ball is constructible from the stable config + geometry contract",
          "[sim][ball][state]") {
  static_assert(std::is_constructible_v<Ball, const BallConfig&,
                                        const football::model::Pitch&>);
  static_assert(std::is_constructible_v<Ball, const football::model::Pitch&>);

  Ball ball(BallConfig{}, kPitch);
  const auto state = ball.state();
  REQUIRE(state.position == Vector3(0.0f, 0.0f, 0.0f));
  REQUIRE(state.velocity == Vector3(0.0f, 0.0f, 0.0f));
  REQUIRE(state.angular_velocity == Vector3(0.0f, 0.0f, 0.0f));
}

TEST_CASE("Reset publishes the observable state", "[sim][ball][state]") {
  Ball ball(kPitch);
  ball.Reset(TestState());

  const auto state = ball.state();
  REQUIRE(state.position == TestState().position);
  REQUIRE(state.velocity == TestState().velocity);
  REQUIRE(state.angular_velocity == TestState().angular_velocity);
  REQUIRE(state.orientation.elements[3] == 1.0f);
}

TEST_CASE("Mirror reflects momentum and position only", "[sim][ball][state]") {
  Ball ball(kPitch);
  ball.Reset(TestState());
  // Legacy mirror contract: momentum and position flip, rotation/orientation
  // are deliberately left untouched by the historical implementation.
  ball.Mirror();

  const auto state = ball.state();
  REQUIRE(state.position == Vector3(-1.0f, -2.0f, 0.5f));
  REQUIRE(state.velocity == Vector3(-3.0f, 1.0f, 0.0f));
}

}  // namespace