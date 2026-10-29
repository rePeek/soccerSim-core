#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "football/player/player_body_profile.hpp"

using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

PlayerDefinition MakePlayer(float height, float weight = 75.0f) {
  PlayerDefinition definition;
  definition.height = height;
  definition.weight = weight;
  return definition;
}

float ExpectedGirth(float height, float weight) {
  const float bmi = weight / (height * height);
  return std::sqrt(bmi / 23.0f);
}

}  // namespace

TEST_CASE("height scale is relative to the baked skeleton reference") {
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.92f)).height_scale ==
          Catch::Approx(1.0f));
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.80f)).height_scale ==
          Catch::Approx(1.80f / 1.92f));
}

TEST_CASE("girth is (bmi / 23)^2, continuous in weight") {
  const float height = 1.80f;
  REQUIRE(MakePlayerBodyProfile(MakePlayer(height, 64.0f)).girth_scale ==
          Catch::Approx(ExpectedGirth(height, 64.0f)));
  REQUIRE(MakePlayerBodyProfile(MakePlayer(height, 75.0f)).girth_scale ==
          Catch::Approx(ExpectedGirth(height, 75.0f)));
  REQUIRE(MakePlayerBodyProfile(MakePlayer(height, 90.0f)).girth_scale ==
          Catch::Approx(ExpectedGirth(height, 90.0f)));

  // Reference BMI gives unit girth.
  REQUIRE(MakePlayerBodyProfile(MakePlayer(height, 23.0f * height * height))
              .girth_scale == Catch::Approx(1.0f));

  // Same height: heavier means wider, with no band edge between them.
  const float slim = MakePlayerBodyProfile(MakePlayer(height, 64.0f)).girth_scale;
  const float normal = MakePlayerBodyProfile(MakePlayer(height, 75.0f)).girth_scale;
  const float strong = MakePlayerBodyProfile(MakePlayer(height, 90.0f)).girth_scale;
  REQUIRE(slim < normal);
  REQUIRE(normal < strong);
}

TEST_CASE("a definition that only carries height keeps the data default weight") {
  // weight defaults to 75.0 kg, so the resulting build follows the height.
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.80f)).girth_scale ==
          Catch::Approx(ExpectedGirth(1.80f, 75.0f)));
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.95f)).girth_scale ==
          Catch::Approx(ExpectedGirth(1.95f, 75.0f)));
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.65f)).girth_scale ==
          Catch::Approx(ExpectedGirth(1.65f, 75.0f)));
}

TEST_CASE("equal BMI means equal girth at any height") {
  const auto shorter = MakePlayerBodyProfile(MakePlayer(1.80f, 75.0f));
  const auto taller = MakePlayerBodyProfile(MakePlayer(1.92f, 85.0f));

  // Both are close to BMI 23, so their girth factors are nearly identical and
  // their heights differ; the exact value follows the formula, not a band.
  REQUIRE(shorter.girth_scale == Catch::Approx(taller.girth_scale).margin(0.02f));
  REQUIRE(shorter.height_scale < taller.height_scale);
}

TEST_CASE("profile is deterministic for equal input") {
  const PlayerBodyProfile a = MakePlayerBodyProfile(MakePlayer(1.8f, 75.0f));
  const PlayerBodyProfile b = MakePlayerBodyProfile(MakePlayer(1.8f, 75.0f));

  REQUIRE(a.height_m == b.height_m);
  REQUIRE(a.height_scale == b.height_scale);
  REQUIRE(a.girth_scale == b.girth_scale);
}

TEST_CASE("invalid inputs are rejected") {
  using limits = std::numeric_limits<float>;

  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(0.0f)), std::invalid_argument);
  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(-1.0f)), std::invalid_argument);
  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(limits::infinity())),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(limits::quiet_NaN())),
                    std::invalid_argument);

  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(1.8f, 0.0f)),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(1.8f, -5.0f)),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(1.8f, limits::infinity())),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(MakePlayerBodyProfile(MakePlayer(1.8f, limits::quiet_NaN())),
                    std::invalid_argument);
}