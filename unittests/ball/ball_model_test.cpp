// BallModel: construction-time parameter validation only.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>

#include "football/ball/ball_model.hpp"

using football::ball::BallModel;
using Catch::Approx;

TEST_CASE("BallModel defaults are physically valid", "[unit][ball][model]") {
  const BallModel model;
  CHECK(model.mass() > 0.0f);
  CHECK(model.radius() > 0.0f);
  CHECK(model.restitution() >= 0.0f);
  CHECK(model.restitution() <= 1.0f);
  CHECK(model.drag() >= 0.0f);
}

TEST_CASE("BallModel accepts explicit valid parameters", "[unit][ball][model]") {
  const BallModel model(0.43f, 0.11f, 0.62f, 0.015f);
  CHECK(model.mass() == Approx(0.43f));
  CHECK(model.radius() == Approx(0.11f));
  CHECK(model.restitution() == Approx(0.62f));
  CHECK(model.drag() == Approx(0.015f));
}

TEST_CASE("BallModel rejects non-positive mass or radius", "[unit][ball][model]") {
  REQUIRE_THROWS_AS(BallModel(0.0f, 0.11f), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(-1.0f, 0.11f), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(0.43f, 0.0f), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(0.43f, -0.11f), std::invalid_argument);
}

TEST_CASE("BallModel rejects out-of-range restitution and negative drag",
          "[unit][ball][model]") {
  REQUIRE_THROWS_AS(BallModel(0.43f, 0.11f, -0.01f), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(0.43f, 0.11f, 1.01f), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(0.43f, 0.11f, 0.62f, -0.01f), std::invalid_argument);
  CHECK_NOTHROW(BallModel(0.43f, 0.11f, 0.0f));
  CHECK_NOTHROW(BallModel(0.43f, 0.11f, 1.0f));
}

TEST_CASE("BallModel rejects non-finite parameters", "[unit][ball][model]") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  REQUIRE_THROWS_AS(BallModel(nan, 0.11f), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(0.43f, inf), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(0.43f, 0.11f, nan), std::invalid_argument);
  REQUIRE_THROWS_AS(BallModel(0.43f, 0.11f, 0.62f, inf), std::invalid_argument);
}
