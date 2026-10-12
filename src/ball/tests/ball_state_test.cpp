#include <cmath>
#include "model/pitch.hpp"
#include <stdexcept>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball.hpp"

#include "football/pitch/pitch.hpp"

namespace {

using football::ball::Ball;
using football::ball::BallModel;
using football::ball::BallState;
using blunted::Vector3;

const football::model::Pitch kPitch = football::model::Pitch{};

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
  static_assert(std::is_constructible_v<Ball, const BallModel&,
                                        football::ball::BallWorld>);

  Ball ball{BallModel{}, football::pitch::BuildBallWorld(kPitch, BallModel{})};
  const auto state = ball.state();
  REQUIRE(state.position == Vector3(0.0f, 0.0f, 0.0f));
  REQUIRE(state.velocity == Vector3(0.0f, 0.0f, 0.0f));
  REQUIRE(state.angular_velocity == Vector3(0.0f, 0.0f, 0.0f));
}

TEST_CASE("Reset publishes the observable state", "[sim][ball][state]") {
  Ball ball{BallModel{}, football::pitch::BuildBallWorld(kPitch, BallModel{})};
  ball.Reset(TestState());

  const auto state = ball.state();
  REQUIRE(state.position == TestState().position);
  REQUIRE(state.velocity == TestState().velocity);
  REQUIRE(state.angular_velocity == TestState().angular_velocity);
  REQUIRE(state.orientation.elements[3] == 1.0f);
}

TEST_CASE("Mirror reflects momentum and position only", "[sim][ball][state]") {
  Ball ball{BallModel{}, football::pitch::BuildBallWorld(kPitch, BallModel{})};
  ball.Reset(TestState());
  // Legacy mirror contract: momentum and position flip, rotation/orientation
  // are deliberately left untouched by the historical implementation.
  ball.Mirror();

  const auto state = ball.state();
  REQUIRE(state.position == Vector3(-1.0f, -2.0f, 0.5f));
  REQUIRE(state.velocity == Vector3(-3.0f, 1.0f, 0.0f));
}

TEST_CASE("invalid physical config is rejected", "[sim][ball][state]") {
  REQUIRE_THROWS_AS(football::ball::BallModel(0.0f, 0.11f, 0.62f, 0.015f), std::invalid_argument);
  REQUIRE_THROWS_AS(football::ball::BallModel(0.43f, -1.0f, 0.62f, 0.015f), std::invalid_argument);
  REQUIRE_THROWS_AS(football::ball::BallModel(std::nanf(""), 0.11f, 0.62f, 0.015f), std::invalid_argument);
}

}  // namespace
