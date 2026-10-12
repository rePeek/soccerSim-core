// Collision CCD geometry: swept sphere vs one collider, plus static overlap.
//
// `toi` is the first contact fraction of [0, 1]; `normal` points from the
// collider surface toward the sphere.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "football/foundation/math/vector3.hpp"

using football::math::Vector3;
using football::collision::CollisionHit;
using football::collision::FirstHit;
using football::collision::MovingSphere;
using football::collision::Overlap;
using football::collision::Sweep;
using football::geometry::Capsule;
using football::geometry::ColliderMotion;
using football::geometry::ContactMaterial;
using football::geometry::Plane;
using football::geometry::Sphere;
using Catch::Approx;

namespace {

constexpr float kBallRadius = 0.11f;
constexpr float kDt = 0.3f;

MovingSphere BallSweep(const Vector3& position, const Vector3& velocity,
                       float radius = kBallRadius) {
  return MovingSphere{{position, radius}, velocity};
}

// True when the sphere approaches the surface (relative velocity into it).
bool Approaching(const MovingSphere& sphere, const CollisionHit& hit) {
  return (sphere.velocity - hit.surface_velocity).GetDotProduct(hit.normal) < 0.0f;
}

}  // namespace

TEST_CASE("Swept sphere hits a static plane", "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -2.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const ColliderMotion collider{1, plane, plane, ContactMaterial{0.5f, 0.0f}};

  const auto hit = Sweep(sphere, collider, 0.5f);
  REQUIRE(hit.has_value());
  CHECK(hit->collider == 1);
  CHECK(hit->toi == Approx(0.89f).margin(1e-4f));
  CHECK(hit->center.coords[2] == Approx(kBallRadius).margin(1e-4f));
  CHECK(hit->normal.coords[2] == Approx(1.0f).margin(1e-5f));
  CHECK(Approaching(sphere, *hit));
}

TEST_CASE("Swept sphere hits a static sphere", "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f}, 0.1f);
  const Sphere target{{0.0f, 0.0f, 1.0f}, 0.4f};
  const ColliderMotion collider{7, target, target, ContactMaterial{0.5f, 0.0f}};

  const auto hit = Sweep(sphere, collider, kDt);
  REQUIRE(hit.has_value());
  CHECK(hit->collider == 7);
  CHECK(hit->toi == Approx(0.5f).margin(1e-5f));
  CHECK(hit->center.coords[0] == Approx(-0.5f).margin(1e-5f));
  CHECK(hit->normal.coords[0] == Approx(-1.0f).margin(1e-5f));
  CHECK(Approaching(sphere, *hit));
}

TEST_CASE("Swept sphere hits a static capsule side", "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f});
  const Capsule capsule{{0.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, 0.3f};
  const ColliderMotion collider{4, capsule, capsule, ContactMaterial{0.5f, 0.0f}};

  const auto hit = Sweep(sphere, collider, kDt);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.53f).margin(2e-3f));
  CHECK(hit->center.coords[0] == Approx(-0.41f).margin(2e-3f));
  CHECK(hit->normal.coords[0] == Approx(-1.0f).margin(1e-3f));
  CHECK(Approaching(sphere, *hit));
}

TEST_CASE("Swept sphere hits a static capsule endpoint", "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f}, 0.1f);
  const Capsule capsule{{0.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 1.0f}, 0.4f};
  const ColliderMotion collider{4, capsule, capsule, ContactMaterial{0.5f, 0.0f}};

  const auto hit = Sweep(sphere, collider, kDt);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.5f).margin(1e-4f));
  CHECK(hit->center.coords[0] == Approx(-0.5f).margin(1e-4f));
}

TEST_CASE("Swept sphere does not tunnel through a thin fast collider",
          "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({-10.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 0.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}};
  const ColliderMotion collider{3, plane, plane, ContactMaterial{0.5f, 0.0f}};

  const auto hit = Sweep(sphere, collider, 0.01f);
  REQUIRE(hit.has_value());
  CHECK(hit->toi > 0.0f);
  CHECK(hit->toi < 1.0f);
  CHECK(hit->collider == 3);
}

TEST_CASE("Swept sphere receding from a tangent plane is not a contact",
          "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const ColliderMotion collider{1, plane, plane, ContactMaterial{0.5f, 0.0f}};
  CHECK_FALSE(Sweep(sphere, collider, 0.5f).has_value());
}

TEST_CASE("Initially penetrating sphere reports toi zero",
          "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({0.0f, 0.0f, 0.05f}, {0.0f, 0.0f, -1.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const ColliderMotion collider{1, plane, plane, ContactMaterial{0.5f, 0.0f}};

  const auto hit = Sweep(sphere, collider, 0.5f);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.0f));
  CHECK(hit->normal.coords[2] == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("FirstHit picks the earliest TOI, then the lowest id",
          "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f}, 0.1f);

  SECTION("earliest wins") {
    const Sphere far{{5.0f, 0.0f, 1.0f}, 0.4f};
    const Sphere near{{0.0f, 0.0f, 1.0f}, 0.4f};
    const std::vector<ColliderMotion> colliders{
        {5, far, far, ContactMaterial{0.5f, 0.0f}},
        {9, near, near, ContactMaterial{0.5f, 0.0f}}};
    const auto hit = FirstHit(sphere, colliders, kDt);
    REQUIRE(hit.has_value());
    CHECK(hit->collider == 9);
  }

  SECTION("equal TOI is broken by id") {
    const Sphere a{{0.0f, 0.0f, 1.0f}, 0.4f};
    const Sphere b{{0.0f, 0.0f, 1.0f}, 0.4f};
    const std::vector<ColliderMotion> colliders{
        {9, a, a, ContactMaterial{0.5f, 0.0f}},
        {3, b, b, ContactMaterial{0.5f, 0.0f}}};
    const auto hit = FirstHit(sphere, colliders, kDt);
    REQUIRE(hit.has_value());
    CHECK(hit->collider == 3);
  }
}

TEST_CASE("Swept sphere accounts for a moving sphere",
          "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({2.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.1f);
  const Sphere start{{0.0f, 0.0f, 1.0f}, 0.4f};
  const Sphere end{{3.0f, 0.0f, 1.0f}, 0.4f};
  const ColliderMotion collider{2, start, end, ContactMaterial{0.5f, 0.0f}};

  const auto hit = Sweep(sphere, collider, kDt);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.5f).margin(1e-4f));
  CHECK(hit->surface_velocity.coords[0] == Approx(10.0f).margin(1e-3f));
  CHECK(Approaching(sphere, *hit));
}

TEST_CASE("Deforming capsule reports contact and non-zero surface motion",
          "[unit][collision][sweep]") {
  const MovingSphere sphere = BallSweep({-1.5f, 0.5f, 1.0f}, {8.0f, 0.0f, 0.0f});

  const Capsule static_start{{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, 0.2f};
  const ColliderMotion static_collider{
      6, static_start, static_start, ContactMaterial{0.5f, 0.0f}};
  const auto static_hit = Sweep(sphere, static_collider, kDt);
  REQUIRE(static_hit.has_value());
  CHECK(static_hit->surface_velocity.GetLength() == Approx(0.0f).margin(1e-5f));

  // Same capsule stretched along its axis: the surface now moves with it.
  const Capsule stretched_end{{0.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 1.0f}, 0.2f};
  const ColliderMotion deforming{
      6, static_start, stretched_end, ContactMaterial{0.5f, 0.0f}};
  const auto deforming_hit = Sweep(sphere, deforming, kDt);
  REQUIRE(deforming_hit.has_value());
  CHECK(deforming_hit->toi > 0.0f);
  CHECK(deforming_hit->toi <= 1.0f);
  CHECK(Approaching(sphere, *deforming_hit));
  CHECK(deforming_hit->surface_velocity.GetLength() > 0.0f);
}

TEST_CASE("Static overlap queries", "[unit][collision][overlap]") {
  const Sphere ball{{0.0f, 0.0f, 0.0f}, 0.5f};

  CHECK(Overlap(ball, Sphere{{0.5f, 0.0f, 0.0f}, 0.5f}));   // touching
  CHECK_FALSE(Overlap(ball, Sphere{{1.5f, 0.0f, 0.0f}, 0.5f}));

  CHECK(Overlap(ball, Capsule{{0.4f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.2f}));
  CHECK_FALSE(Overlap(ball, Capsule{{2.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, 0.2f}));

  CHECK(Overlap(ball, Plane{{0.0f, 0.0f, 0.4f}, {0.0f, 0.0f, 1.0f}}));
  CHECK_FALSE(Overlap(ball, Plane{{0.0f, 0.0f, 0.6f}, {0.0f, 0.0f, 1.0f}}));
}
