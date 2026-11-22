
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "football/foundation/math/vector3.hpp"

using football::collision::FirstHit;
using football::collision::MovingSphere;
using football::collision::Sweep;
using football::geometry::Capsule;
using football::geometry::Collider;
using football::geometry::HalfSpace;
using football::math::Vector3;
using Catch::Approx;

namespace {

constexpr float kDt = 0.01f;
constexpr float kBallRadius = 0.11f;

MovingSphere BallAt(const Vector3& position, const Vector3& velocity) {
  return MovingSphere{{position, kBallRadius}, velocity};
}

Collider Ground() {
  return Collider{HalfSpace{Vector3(0, 0, 0), Vector3(0, 0, 1)}, {}, "ground"};
}

Collider Wall(float x, std::string_view name) {
  return Collider{HalfSpace{Vector3(x, 0, 0), Vector3(-1, 0, 0)}, {}, name};
}

}

TEST_CASE("A sweep reports the contact time, point and normal", "[unit][collision]") {
  const Capsule post{Vector3(1, 0, 0), Vector3(1, 0, 2), 0.05f};
  const auto hit = Sweep(BallAt({0, 0, 1}, {100, 0, 0}), post, kDt);

  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.84f).margin(1e-4f));
  CHECK(hit->center.coords[0] == Approx(0.84f).margin(1e-4f));
  CHECK(hit->normal.coords[0] == Approx(-1.0f).margin(1e-4f));
  CHECK(hit->surface_velocity.GetLength() == Approx(0.0f));
}

TEST_CASE("A sweep that misses returns nothing", "[unit][collision]") {
  const Capsule post{Vector3(5, 0, 0), Vector3(5, 0, 2), 0.05f};
  CHECK_FALSE(Sweep(BallAt({0, 0, 1}, {100, 0, 0}), post, kDt).has_value());
}

TEST_CASE("A sweep rejects malformed input instead of reporting a miss",
          "[unit][collision]") {
  const Capsule post{Vector3(1, 0, 0), Vector3(1, 0, 2), 0.05f};
  CHECK_THROWS_AS(Sweep(BallAt({0, 0, 1}, {100, 0, 0}), post, 0.0f),
                  std::invalid_argument);
  CHECK_THROWS_AS(Sweep(BallAt({0, 0, 1}, {100, 0, 0}), post, -kDt),
                  std::invalid_argument);
}

TEST_CASE("FirstHit keeps the earliest contact across the set", "[unit][collision]") {
  const std::array colliders{Wall(0.9f, "far"), Wall(0.5f, "near")};
  const auto found = FirstHit(BallAt({0, 0, 1}, {100, 0, 0}), colliders, kDt);

  REQUIRE(found.has_value());
  CHECK(found->collider == &colliders[1]);
  CHECK(found->collider->name == "near");
  CHECK(found->hit.toi == Approx(0.39f).margin(1e-4f));
}

TEST_CASE("FirstHit picks the same timing when the set is reordered",
          "[unit][collision]") {
  const std::array forward{Wall(0.5f, "near"), Wall(0.9f, "far")};
  const std::array reversed{Wall(0.9f, "far"), Wall(0.5f, "near")};

  const auto first = FirstHit(BallAt({0, 0, 1}, {100, 0, 0}), forward, kDt);
  const auto second = FirstHit(BallAt({0, 0, 1}, {100, 0, 0}), reversed, kDt);

  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK(first->hit.toi == Approx(second->hit.toi));
  CHECK(first->collider->name == "near");
  CHECK(second->collider->name == "near");
}

TEST_CASE("An exact tie keeps the collider met first in the set", "[unit][collision]") {
  const std::array forward{Wall(0.5f, "first"), Wall(0.5f, "second")};
  const std::array reversed{Wall(0.5f, "second"), Wall(0.5f, "first")};

  CHECK(FirstHit(BallAt({0, 0, 1}, {100, 0, 0}), forward, kDt)->collider->name ==
        "first");
  CHECK(FirstHit(BallAt({0, 0, 1}, {100, 0, 0}), reversed, kDt)->collider->name ==
        "second");
}

TEST_CASE("Names never affect a query", "[unit][collision]") {
  const std::array anonymous{Collider{HalfSpace{Vector3(0.5f, 0, 0), Vector3(-1, 0, 0)},
                                      {}, {}},
                             Collider{HalfSpace{Vector3(0.5f, 0, 0), Vector3(-1, 0, 0)},
                                      {}, {}}};
  const std::array duplicated{Collider{HalfSpace{Vector3(0.5f, 0, 0), Vector3(-1, 0, 0)},
                                       {}, "same"},
                              Collider{HalfSpace{Vector3(0.5f, 0, 0), Vector3(-1, 0, 0)},
                                       {}, "same"}};

  const auto blank = FirstHit(BallAt({0, 0, 1}, {100, 0, 0}), anonymous, kDt);
  const auto named = FirstHit(BallAt({0, 0, 1}, {100, 0, 0}), duplicated, kDt);

  REQUIRE(blank.has_value());
  REQUIRE(named.has_value());
  CHECK(blank->hit.toi == Approx(named->hit.toi));
  CHECK(blank->collider == &anonymous[0]);
  CHECK(named->collider == &duplicated[0]);
}

TEST_CASE("Empty sets and misses yield no hit", "[unit][collision]") {
  const std::vector<Collider> none;
  CHECK_FALSE(FirstHit(BallAt({0, 0, 1}, {10, 0, 0}), none, kDt).has_value());

  const std::array colliders{Ground()};
  CHECK_FALSE(FirstHit(BallAt({0, 0, 1}, {0, 0, 10}), colliders, kDt).has_value());
}

TEST_CASE("Repeating a query is deterministic", "[unit][collision]") {
  const std::array colliders{Ground(), Wall(0.5f, "near"), Wall(0.5f, "twin")};
  const auto sphere = BallAt({0, 0, 0.02f}, {10, 0, -3});
  const auto first = FirstHit(sphere, colliders, kDt);
  for (int i = 0; i < 16; ++i) {
    const auto again = FirstHit(sphere, colliders, kDt);
    REQUIRE(again.has_value());
    CHECK(again->hit.toi == first->hit.toi);
    CHECK(again->hit.normal == first->hit.normal);
    CHECK(again->collider == first->collider);
  }
}
