// Ball CCD geometry: swept sphere vs one collider, analytic expectations.
//
// `toi` is the first contact fraction of the swept interval [0, 1]; `normal`
// points from the collider surface toward the ball.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_model.hpp"
#include "football/ball/ball_state.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/geometry/collider.hpp"

using blunted::Vector3;
using football::ball::FirstContact;
using football::ball::SweepBall;
using football::geometry::Capsule;
using football::geometry::ColliderMotion;
using football::geometry::ContactMaterial;
using football::geometry::Plane;
using football::geometry::Sphere;
using Catch::Approx;

namespace {

football::ball::BallState BallAt(const Vector3& position, const Vector3& velocity) {
  football::ball::BallState state;
  state.position = position;
  state.velocity = velocity;
  return state;
}

constexpr float kBallRadius = 0.11f;
constexpr float kDt = 0.3f;

}  // namespace

TEST_CASE("Swept ball hits a static plane", "[unit][ball][ccd]") {
  const auto ball = BallAt({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -2.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const ColliderMotion collider{1, plane, plane, ContactMaterial{0.5f, 0.0f}};

  const auto hit = SweepBall(ball, collider, 0.5f, kBallRadius);
  REQUIRE(hit.has_value());
  CHECK(hit->collider == 1);
  CHECK(hit->toi == Approx(0.89f).margin(1e-4f));
  CHECK(hit->point.coords[2] == Approx(kBallRadius).margin(1e-4f));
  CHECK(hit->normal.coords[2] == Approx(1.0f).margin(1e-5f));
  CHECK(hit->relative_velocity.GetDotProduct(hit->normal) < 0.0f);
}

TEST_CASE("Swept ball hits a static sphere", "[unit][ball][ccd]") {
  const auto ball = BallAt({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f});
  const Sphere sphere{{0.0f, 0.0f, 1.0f}, 0.4f};
  const ColliderMotion collider{7, sphere, sphere, ContactMaterial{0.5f, 0.0f}};

  const auto hit = SweepBall(ball, collider, kDt, 0.1f);
  REQUIRE(hit.has_value());
  CHECK(hit->collider == 7);
  CHECK(hit->toi == Approx(0.5f).margin(1e-5f));
  CHECK(hit->point.coords[0] == Approx(-0.5f).margin(1e-5f));
  CHECK(hit->normal.coords[0] == Approx(-1.0f).margin(1e-5f));
  CHECK(hit->relative_velocity.GetDotProduct(hit->normal) < 0.0f);
}

TEST_CASE("Swept ball hits a static capsule side", "[unit][ball][ccd]") {
  const auto ball = BallAt({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f});
  const Capsule capsule{{0.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, 0.3f};
  const ColliderMotion collider{4, capsule, capsule, ContactMaterial{0.5f, 0.0f}};

  const auto hit = SweepBall(ball, collider, kDt, kBallRadius);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.53f).margin(2e-3f));
  CHECK(hit->point.coords[0] == Approx(-0.41f).margin(2e-3f));
  CHECK(hit->normal.coords[0] == Approx(-1.0f).margin(1e-3f));
  CHECK(hit->relative_velocity.GetDotProduct(hit->normal) < 0.0f);
}

TEST_CASE("Swept ball hits a static capsule endpoint", "[unit][ball][ccd]") {
  const auto ball = BallAt({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f});
  const Capsule capsule{{0.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 1.0f}, 0.4f};
  const ColliderMotion collider{4, capsule, capsule, ContactMaterial{0.5f, 0.0f}};

  const auto hit = SweepBall(ball, collider, kDt, 0.1f);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.5f).margin(1e-4f));
  CHECK(hit->point.coords[0] == Approx(-0.5f).margin(1e-4f));
}

TEST_CASE("Swept ball does not tunnel through a thin fast collider",
          "[unit][ball][ccd]") {
  const auto ball = BallAt({-10.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 0.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}};
  const ColliderMotion collider{3, plane, plane, ContactMaterial{0.5f, 0.0f}};

  const auto hit = SweepBall(ball, collider, 0.01f, kBallRadius);
  REQUIRE(hit.has_value());
  CHECK(hit->toi > 0.0f);
  CHECK(hit->toi < 1.0f);
  CHECK(hit->collider == 3);
}

TEST_CASE("Swept ball receding from a tangent plane is not a contact",
          "[unit][ball][ccd]") {
  const auto ball = BallAt({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const ColliderMotion collider{1, plane, plane, ContactMaterial{0.5f, 0.0f}};
  CHECK_FALSE(SweepBall(ball, collider, 0.5f, kBallRadius).has_value());
}

TEST_CASE("Initially penetrating ball reports toi zero", "[unit][ball][ccd]") {
  const auto ball = BallAt({0.0f, 0.0f, 0.05f}, {0.0f, 0.0f, -1.0f});
  const Plane plane{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const ColliderMotion collider{1, plane, plane, ContactMaterial{0.5f, 0.0f}};

  const auto hit = SweepBall(ball, collider, 0.5f, kBallRadius);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.0f));
  CHECK(hit->normal.coords[2] == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("FirstContact picks the earliest TOI, then the lowest id",
          "[unit][ball][ccd]") {
  const auto ball = BallAt({-2.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f});

  SECTION("earliest wins") {
    const Sphere far{{5.0f, 0.0f, 1.0f}, 0.4f};
    const Sphere near{{0.0f, 0.0f, 1.0f}, 0.4f};
    const std::vector<ColliderMotion> colliders{
        {5, far, far, ContactMaterial{0.5f, 0.0f}},
        {9, near, near, ContactMaterial{0.5f, 0.0f}}};
    const auto hit = FirstContact(ball, colliders, kDt, 0.1f);
    REQUIRE(hit.has_value());
    CHECK(hit->collider == 9);
  }

  SECTION("equal TOI is broken by id") {
    const Sphere a{{0.0f, 0.0f, 1.0f}, 0.4f};
    const Sphere b{{0.0f, 0.0f, 1.0f}, 0.4f};
    const std::vector<ColliderMotion> colliders{
        {9, a, a, ContactMaterial{0.5f, 0.0f}},
        {3, b, b, ContactMaterial{0.5f, 0.0f}}};
    const auto hit = FirstContact(ball, colliders, kDt, 0.1f);
    REQUIRE(hit.has_value());
    CHECK(hit->collider == 3);
  }
}

TEST_CASE("Swept ball accounts for a moving sphere", "[unit][ball][ccd]") {
  const auto ball = BallAt({2.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f});
  const Sphere start{{0.0f, 0.0f, 1.0f}, 0.4f};
  const Sphere end{{3.0f, 0.0f, 1.0f}, 0.4f};
  const ColliderMotion collider{2, start, end, ContactMaterial{0.5f, 0.0f}};

  const auto hit = SweepBall(ball, collider, kDt, 0.1f);
  REQUIRE(hit.has_value());
  CHECK(hit->toi == Approx(0.5f).margin(1e-4f));
  CHECK(hit->surface_velocity.coords[0] == Approx(10.0f).margin(1e-3f));
  CHECK(hit->relative_velocity.GetDotProduct(hit->normal) < 0.0f);
}

TEST_CASE("Deforming capsule reports contact and non-zero surface motion",
          "[unit][ball][ccd]") {
  const auto ball = BallAt({-1.5f, 0.5f, 1.0f}, {8.0f, 0.0f, 0.0f});

  const Capsule static_start{{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, 0.2f};
  const ColliderMotion static_collider{
      6, static_start, static_start, ContactMaterial{0.5f, 0.0f}};
  const auto static_hit = SweepBall(ball, static_collider, kDt, kBallRadius);
  REQUIRE(static_hit.has_value());
  CHECK(static_hit->surface_velocity.GetLength() == Approx(0.0f).margin(1e-5f));

  // Same capsule stretched along its axis: the surface now moves with it.
  const Capsule stretched_end{{0.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 1.0f}, 0.2f};
  const ColliderMotion deforming{
      6, static_start, stretched_end, ContactMaterial{0.5f, 0.0f}};
  const auto deforming_hit = SweepBall(ball, deforming, kDt, kBallRadius);
  REQUIRE(deforming_hit.has_value());
  CHECK(deforming_hit->toi > 0.0f);
  CHECK(deforming_hit->toi <= 1.0f);
  CHECK(deforming_hit->relative_velocity.GetDotProduct(deforming_hit->normal) < 0.0f);
  CHECK(deforming_hit->surface_velocity.GetLength() > 0.0f);
}
