// TACT-2: canonical tactical coordinates <-> observed world metres.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/ai/tactical_interpreter.hpp"

namespace {

using Catch::Approx;
using football::ai::TacticalAnchor;
using football::ai::TacticalAnchorToWorld;
using football::ai::WorldPositionToAnchor;
using football::math::Vector3;

const football::pitch::Pitch kPitch;

}  // namespace

TEST_CASE("u runs from own goal to opponent goal in both frames",
          "[ai][tactical][coordinate]") {
  // Home defends -x, so u=0 is the negative goal line.
  const Vector3 own = TacticalAnchorToWorld({0.0f, 0.0f}, -1, kPitch);
  const Vector3 opp = TacticalAnchorToWorld({1.0f, 0.0f}, -1, kPitch);
  CHECK(own.coords[0] == Approx(-kPitch.half_length()));
  CHECK(opp.coords[0] == Approx(kPitch.half_length()));

  // Away defends +x, so the canonical plan mirrors without a separate copy.
  const Vector3 away_own = TacticalAnchorToWorld({0.0f, 0.0f}, 1, kPitch);
  const Vector3 away_opp = TacticalAnchorToWorld({1.0f, 0.0f}, 1, kPitch);
  CHECK(away_own.coords[0] == Approx(kPitch.half_length()));
  CHECK(away_opp.coords[0] == Approx(-kPitch.half_length()));
}

TEST_CASE("home and away interpretations are point mirrors",
          "[ai][tactical][coordinate]") {
  for (float u : {0.0f, 0.25f, 0.5f, 0.8f, 1.0f}) {
    for (float v : {-1.0f, -0.4f, 0.0f, 0.3f, 1.0f}) {
      const Vector3 home = TacticalAnchorToWorld({u, v}, -1, kPitch);
      const Vector3 away = TacticalAnchorToWorld({u, v}, 1, kPitch);
      CHECK(home.coords[0] == Approx(-away.coords[0]));
      CHECK(home.coords[1] == Approx(-away.coords[1]));
    }
  }
}

TEST_CASE("anchor round-trips through the world frame",
          "[ai][tactical][coordinate]") {
  for (int direction : {-1, 1}) {
    for (float u : {0.1f, 0.5f, 0.93f}) {
      for (float v : {-0.8f, 0.0f, 0.6f}) {
        const TacticalAnchor anchor{u, v};
        const Vector3 world = TacticalAnchorToWorld(anchor, direction, kPitch);
        const TacticalAnchor back = WorldPositionToAnchor(world, direction, kPitch);
        CHECK(back.u == Approx(anchor.u));
        CHECK(back.v == Approx(anchor.v));
      }
    }
  }
}

TEST_CASE("v sign follows the attacking direction", "[ai][tactical][coordinate]") {
  // Attacking +x (home), own left is +y. Attacking -x (away), own left is -y.
  CHECK(TacticalAnchorToWorld({0.5f, 1.0f}, -1, kPitch).coords[1] ==
        Approx(kPitch.half_width()));
  CHECK(TacticalAnchorToWorld({0.5f, 1.0f}, 1, kPitch).coords[1] ==
        Approx(-kPitch.half_width()));
}
