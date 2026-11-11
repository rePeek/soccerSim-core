// BallModel is editable configuration copied by Ball without validation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "football/ball/ball.hpp"

using football::ball::Ball;
using football::ball::BallModel;
using Catch::Approx;

static_assert(std::is_aggregate_v<BallModel>);
static_assert(std::is_standard_layout_v<BallModel>);
static_assert(std::is_trivially_copyable_v<BallModel>);
static_assert(std::is_same_v<BallModel, football_ball_model>);

TEST_CASE("BallModel defaults are explicit values", "[unit][ball][model]") {
  const BallModel model{0.43f, 0.11f, 0.02f};
  CHECK(model.mass == Approx(0.43f));
  CHECK(model.radius == Approx(0.11f));
  CHECK(model.magnus_coefficient == Approx(0.02f));

  const BallModel held = Ball{}.model();
  CHECK(held.mass == model.mass);
  CHECK(held.radius == model.radius);
  CHECK(held.magnus_coefficient == model.magnus_coefficient);
}

TEST_CASE("Ball owns a copy of editable model values", "[unit][ball][model]") {
  BallModel model{0.43f, 0.11f, 0.02f};
  model.magnus_coefficient = 0.05f;
  model.radius = 0.12f;
  const Ball ball(model);
  const BallModel held = ball.model();
  CHECK(held.mass == model.mass);
  CHECK(held.radius == model.radius);
  CHECK(held.magnus_coefficient == model.magnus_coefficient);

  model.mass = 0.0f;
  model.radius = 0.0f;
  CHECK(ball.model().mass == Approx(0.43f));
  CHECK(ball.model().radius == Approx(0.12f));
}

TEST_CASE("Shared ball model has no hidden defaults", "[unit][ball][model]") {
  const BallModel zero{};
  CHECK(zero.mass == 0.0f);
  CHECK(zero.radius == 0.0f);
  CHECK(zero.magnus_coefficient == 0.0f);
}
