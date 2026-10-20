// football::pitch::Pitch — the domain that owns the static physics scene.
//
// The pitch builds the ground (a solid half-space) and the goal furniture
// (four posts + two crossbars) once at construction, and exposes them as a
// read-only, deterministically ordered list. These tests pin that contract
// without creating a Ball or a Simulation.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <variant>

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "football/pitch/pitch_frame.hpp"

using football::geometry::Capsule;
using football::geometry::ColliderMotion;
using football::geometry::HalfSpace;
using football::math::Vector3;
using football::pitch::Pitch;
using Catch::Approx;

TEST_CASE("Default pitch builds seven ordered static colliders", "[unit][pitch]") {
  const Pitch pitch;
  REQUIRE(pitch.colliders().size() == 7);

  // Ids are 1..7, unique, in a fixed order (also the CCD tie-break order).
  for (std::size_t i = 0; i < pitch.colliders().size(); ++i) {
    CHECK(pitch.colliders()[i].id == i + 1);
    for (std::size_t j = i + 1; j < pitch.colliders().size(); ++j) {
      CHECK(pitch.colliders()[i].id != pitch.colliders()[j].id);
    }
    // Static: start and end hold the same shape.
    CHECK(pitch.colliders()[i].start == pitch.colliders()[i].end);
  }

  // Ground first: a solid half-space at z = 0 facing up.
  const ColliderMotion& ground = pitch.colliders().front();
  REQUIRE(std::holds_alternative<HalfSpace>(ground.start));
  const auto& half_space = std::get<HalfSpace>(ground.start);
  CHECK(half_space.point == Vector3(0.0f, 0.0f, 0.0f));
  CHECK(half_space.normal == Vector3(0.0f, 0.0f, 1.0f));

  // Goal furniture is capsules (four posts + two crossbars).
  for (std::size_t i = 1; i < pitch.colliders().size(); ++i) {
    CHECK(std::holds_alternative<Capsule>(pitch.colliders()[i].start));
  }
}

TEST_CASE("Goal colliders follow the pitch geometry", "[unit][pitch]") {
  const Pitch pitch;
  const float x = pitch.half_length();
  const float y = pitch.goal_half_width();
  const float z = pitch.goal_height();

  const auto capsule = [&](std::size_t i) {
    return std::get<Capsule>(pitch.colliders()[i].start);
  };

  // Posts: ids 2..5, vertical bars from the ground to the crossbar height.
  CHECK(capsule(1).a == Vector3(-x, -y, 0.0f));
  CHECK(capsule(1).b == Vector3(-x, -y, z));
  CHECK(capsule(2).a == Vector3(-x, y, 0.0f));
  CHECK(capsule(3).a == Vector3(x, -y, 0.0f));
  CHECK(capsule(4).a == Vector3(x, y, 0.0f));
  for (std::size_t i = 1; i <= 4; ++i) {
    CHECK(capsule(i).radius == Approx(0.06f));
  }

  // Crossbars: ids 6 and 7, spanning the goal mouth at the top of the posts.
  CHECK(capsule(5).a == Vector3(-x, -y, z));
  CHECK(capsule(5).b == Vector3(-x, y, z));
  CHECK(capsule(6).a == Vector3(x, -y, z));
  CHECK(capsule(6).b == Vector3(x, y, z));
}

TEST_CASE("Configuration drives the derived geometry", "[unit][pitch]") {
  const Pitch small(90.0f, 60.0f);
  CHECK(small.length() == Approx(90.0f));
  CHECK(small.width() == Approx(60.0f));
  CHECK(small.half_length() == Approx(45.0f));
  CHECK(small.half_width() == Approx(30.0f));
  CHECK(small.contains(45.0f, 30.0f));
  CHECK(small.contains(-45.0f, -30.0f));
  CHECK_FALSE(small.contains(45.1f, 0.0f));
  CHECK_FALSE(small.contains(0.0f, 30.1f));

  // The goal posts move with the custom length.
  const auto post = std::get<Capsule>(small.colliders()[1].start);
  CHECK(post.a.coords[0] == Approx(-45.0f));
}

TEST_CASE("Pitch rejects invalid configuration", "[unit][pitch]") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK_THROWS_AS(Pitch(0.0f), std::invalid_argument);
  CHECK_THROWS_AS(Pitch(-105.0f), std::invalid_argument);
  CHECK_THROWS_AS(Pitch(105.0f, 0.0f), std::invalid_argument);
  CHECK_THROWS_AS(Pitch(nan), std::invalid_argument);
  CHECK_THROWS_AS(Pitch(105.0f, 68.0f, -0.1f), std::invalid_argument);
  CHECK_THROWS_AS(Pitch(105.0f, 68.0f, 0.04f, -1.0f), std::invalid_argument);
  CHECK_THROWS_AS(Pitch(105.0f, 68.0f, 0.04f, 1.6f, 0.0f), std::invalid_argument);
}

TEST_CASE("Pitch equality compares configuration, not the collider cache",
          "[unit][pitch]") {
  const Pitch a;
  const Pitch b;
  CHECK(a == b);

  const Pitch other(100.0f, 68.0f);
  CHECK(a != other);
  CHECK(other == Pitch(100.0f, 68.0f));
}

TEST_CASE("Frame transforms are 180-degree rotations about Z", "[unit][pitch]") {
  const Vector3 position(3.0f, -4.0f, 1.5f);
  CHECK(PitchFrameTransform{false}.Position(position) == position);
  CHECK(PitchFrameTransform{false}.Direction(position) == position);

  const Vector3 rotated = PitchFrameTransform{true}.Position(position);
  CHECK(rotated == Vector3(-3.0f, 4.0f, 1.5f));
  // Position and direction share one rotation; Z (height) is preserved.
  CHECK(PitchFrameTransform{true}.Direction(position) == rotated);
  // Involution and isometry: no reflection (handedness) is introduced.
  CHECK(PitchFrameTransform{true}.Position(rotated) == position);
  CHECK(rotated.GetLength() == Approx(position.GetLength()));
}

TEST_CASE("Home pitch frame keeps Home on -X for both processing frames",
          "[unit][pitch]") {
  const Vector3 position(10.0f, 5.0f, 0.0f);
  const Vector3 mirrored(-10.0f, -5.0f, 0.0f);
  // Home in the canonical frame is already the home pitch frame; the other
  // three combinations rotate exactly once.
  CHECK(ToHomePitchFrame(true, -1).Position(position) == position);
  CHECK(ToHomePitchFrame(true, 1).Position(position) == mirrored);
  CHECK(ToHomePitchFrame(false, -1).Position(position) == mirrored);
  CHECK(ToHomePitchFrame(false, 1).Position(position) == position);
  CHECK(FromHomePitchFrame(true, 1).Position(mirrored) == position);
}

TEST_CASE("Attacking frame maps one tactical anchor into either defending end",
          "[unit][pitch]") {
  // A left-back anchor in the team's own attacking frame: 25 m behind the
  // halfway line, on the team's left. The same value is used all match.
  const Vector3 anchor(-25.0f, 20.0f, 0.0f);
  // Defending -X: the attacking frame already equals the fixed frame.
  CHECK(ToAttackingFrame(DefendingEnd::NegativeX).Position(anchor) == anchor);
  // Defending +X: the team plays the other way, so the fixed-frame target is
  // the 180-degree rotation of the same tactical anchor.
  CHECK(ToAttackingFrame(DefendingEnd::PositiveX).Position(anchor) ==
        Vector3(25.0f, -20.0f, 0.0f));
  // Self-inverse: a fixed-frame point maps back into the attacker's view.
  CHECK(FromAttackingFrame(DefendingEnd::PositiveX)
            .Position(Vector3(25.0f, -20.0f, 0.0f)) == anchor);
}
