#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "football/player/player_body_profile.hpp"

using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;
using football::player::GirthScaleForBmi;
using football::player::kSlimGirthScale;
using football::player::kStrongGirthScale;

namespace {

PlayerDefinition MakePlayer(float height, float weight = 75.0f) {
  PlayerDefinition definition;
  definition.height = height;
  definition.weight_kg = weight;
  return definition;
}

}  // namespace

TEST_CASE("height scale is relative to the baked skeleton reference") {
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.92f)).height_scale ==
          Catch::Approx(1.0f));
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.80f)).height_scale ==
          Catch::Approx(1.80f / 1.92f));
}

TEST_CASE("girth is a three-band step of BMI, boundaries stay reference") {
  // BMI = weight / height^2. At 1.80 m: 20 -> 64.8 kg, 25 -> 81.0 kg.
  REQUIRE(GirthScaleForBmi(19.99f) == kSlimGirthScale);
  REQUIRE(GirthScaleForBmi(20.00f) == 1.0f);
  REQUIRE(GirthScaleForBmi(24.99f) == 1.0f);
  REQUIRE(GirthScaleForBmi(25.00f) == 1.0f);
  REQUIRE(GirthScaleForBmi(25.01f) == kStrongGirthScale);

  const float height = 1.80f;
  REQUIRE(MakePlayerBodyProfile(MakePlayer(height, 60.0f)).girth_scale ==
          kSlimGirthScale);
  REQUIRE(MakePlayerBodyProfile(MakePlayer(height, 75.0f)).girth_scale == 1.0f);
  REQUIRE(MakePlayerBodyProfile(MakePlayer(height, 90.0f)).girth_scale ==
          kStrongGirthScale);
}

TEST_CASE("a definition that only carries height keeps the data default weight") {
  // weight_kg defaults to 75.0 kg, so the resulting build follows the height.
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.80f)).girth_scale == 1.0f);
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.95f)).girth_scale == kSlimGirthScale);
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.65f)).girth_scale == kStrongGirthScale);
}

TEST_CASE("equal BMI means equal girth at any height") {
  // 75 kg / 1.80 m and 85 kg / 1.92 m are both in the reference BMI band.
  const auto shorter = MakePlayerBodyProfile(MakePlayer(1.80f, 75.0f));
  const auto taller = MakePlayerBodyProfile(MakePlayer(1.92f, 85.0f));

  REQUIRE(shorter.girth_scale == taller.girth_scale);
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