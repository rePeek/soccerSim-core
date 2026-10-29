#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "football/player/player_body_profile.hpp"

using football::player::BodyBuild;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;
using football::player::kReferenceBmi;

namespace {

PlayerDefinition MakePlayer(float height, std::optional<float> weight = std::nullopt) {
  PlayerDefinition definition;
  definition.height = height;
  definition.weight_kg = std::move(weight);
  return definition;
}

}  // namespace

TEST_CASE("profile derives height scale and standard default girth") {
  const PlayerBodyProfile profile = MakePlayerBodyProfile(MakePlayer(1.8f));

  REQUIRE(profile.height_m == 1.8f);
  REQUIRE(profile.height_scale == Catch::Approx(1.8f / 1.92f));
  REQUIRE(profile.mass_kg == Catch::Approx(23.0f * 1.8f * 1.8f));
  REQUIRE(profile.girth_scale == Catch::Approx(1.0f));
  REQUIRE(profile.build == BodyBuild::Normal);
}

TEST_CASE("a definition without a mass keeps the reference girth at any height") {
  for (const float height : {1.65f, 1.80f, 1.95f, 2.10f}) {
    const PlayerBodyProfile profile = MakePlayerBodyProfile(MakePlayer(height));
    INFO("height " << height);
    REQUIRE(profile.girth_scale == Catch::Approx(1.0f));
    REQUIRE(profile.mass_kg == Catch::Approx(kReferenceBmi * height * height));
    REQUIRE(profile.height_scale == Catch::Approx(height / 1.92f));
    REQUIRE(profile.build == BodyBuild::Normal);
  }
}

TEST_CASE("explicit weight changes girth but never the height scale") {
  const PlayerBodyProfile slim = MakePlayerBodyProfile(MakePlayer(1.8f, 64.0f));
  const PlayerBodyProfile normal = MakePlayerBodyProfile(MakePlayer(1.8f, 75.0f));
  const PlayerBodyProfile broad = MakePlayerBodyProfile(MakePlayer(1.8f, 90.0f));

  REQUIRE(slim.height_scale == normal.height_scale);
  REQUIRE(normal.height_scale == broad.height_scale);

  REQUIRE(slim.girth_scale < normal.girth_scale);
  REQUIRE(normal.girth_scale < broad.girth_scale);

  REQUIRE(slim.build == BodyBuild::Slim);
  REQUIRE(normal.build == BodyBuild::Normal);
  REQUIRE(broad.build == BodyBuild::Broad);
}

TEST_CASE("nine body profiles classify slim/normal/broad") {
  struct Case {
    float height;
    float mass;
    BodyBuild build;
  };
  const Case cases[] = {
      {1.65f, 53.0f, BodyBuild::Slim},
      {1.65f, 63.0f, BodyBuild::Normal},
      {1.65f, 78.0f, BodyBuild::Broad},
      {1.80f, 64.0f, BodyBuild::Slim},
      {1.80f, 75.0f, BodyBuild::Normal},
      {1.80f, 90.0f, BodyBuild::Broad},
      {1.95f, 75.0f, BodyBuild::Slim},
      {1.95f, 88.0f, BodyBuild::Normal},
      {1.95f, 105.0f, BodyBuild::Broad},
  };

  for (const Case& c : cases) {
    const PlayerBodyProfile profile = MakePlayerBodyProfile(MakePlayer(c.height, c.mass));
    INFO("height " << c.height << " mass " << c.mass);
    REQUIRE(profile.build == c.build);
  }
}

TEST_CASE("same mass across heights spans all three builds") {
  // 75 kg is Broad at 1.65 m, Normal at 1.80 m and Slim at 1.95 m under the
  // reference-BMI anchor, so build is a property of the pair, not mass alone.
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.95f, 75.0f)).build == BodyBuild::Slim);
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.80f, 75.0f)).build == BodyBuild::Normal);
  REQUIRE(MakePlayerBodyProfile(MakePlayer(1.65f, 75.0f)).build == BodyBuild::Broad);
}

TEST_CASE("girth scale equals sqrt(mass / reference-BMI * height^2)") {
  const float height = 1.8f;
  const float mass = 75.0f;
  const PlayerBodyProfile profile = MakePlayerBodyProfile(MakePlayer(height, mass));

  REQUIRE(profile.mass_kg == Catch::Approx(mass));
  REQUIRE(profile.girth_scale ==
          Catch::Approx(std::sqrt(mass / (kReferenceBmi * height * height))));
}

TEST_CASE("profile is deterministic for equal input") {
  const PlayerBodyProfile a = MakePlayerBodyProfile(MakePlayer(1.8f, 75.0f));
  const PlayerBodyProfile b = MakePlayerBodyProfile(MakePlayer(1.8f, 75.0f));

  REQUIRE(a.height_m == b.height_m);
  REQUIRE(a.mass_kg == b.mass_kg);
  REQUIRE(a.height_scale == b.height_scale);
  REQUIRE(a.girth_scale == b.girth_scale);
  REQUIRE(a.build == b.build);
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
