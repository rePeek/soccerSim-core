#include <array>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball_contact.hpp"
#include "football/ball/pitch_colliders.hpp"
#include "model/ball_config.hpp"
#include "model/pitch.hpp"

namespace {

using blunted::Vector3;
using football::ball::BallContact;
using football::ball::BallState;
using football::ball::Capsule;
using football::ball::ColliderMotion;
using football::ball::FirstContact;
using football::ball::Plane;
using football::ball::Sphere;
using football::ball::SweepBall;

BallState BallAt(Vector3 position, Vector3 velocity) {
  BallState ball;
  ball.position = position;
  ball.velocity = velocity;
  return ball;
}

ColliderMotion StaticSphere(std::uint32_t id, Vector3 center, float radius) {
  ColliderMotion m;
  m.id = id;
  m.start = Sphere{center, radius};
  m.end = m.start;
  return m;
}

}  // namespace

TEST_CASE("sphere-plane CCD detects a fast ball that tunnels across the plane",
          "[ball][collision][ccd]") {
  const ColliderMotion ground;
  ColliderMotion plane;
  plane.id = 1;
  plane.start = Plane{Vector3(0, 0, 0), Vector3(0, 0, 1)};
  plane.end = plane.start;

  const BallState ball = BallAt(Vector3(0, 0, 1), Vector3(0, 0, -100));
  const auto hit = SweepBall(ball, plane, 0.01f, 0.11f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->collider == 1);
  REQUIRE(hit->toi > 0.0f);
  REQUIRE(hit->toi < 1.0f);
  REQUIRE(hit->normal.coords[2] > 0.0f);
}

TEST_CASE("sphere-sphere CCD accounts for relative motion", "[ball][collision][ccd]") {
  // Opposite motion: ball +x at 100 m/s, collider center moves -x by 1 m/tick.
  ColliderMotion sphere = StaticSphere(3, Vector3(1, 0, 0), 0.3f);
  sphere.end = Sphere{Vector3(0, 0, 0), 0.3f};

  const BallState ball = BallAt(Vector3(0, 0, 0), Vector3(100, 0, 0));
  const auto hit = SweepBall(ball, sphere, 0.01f, 0.11f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->collider == 3);
  REQUIRE(std::fabs(hit->toi - 0.295f) < 0.01f);

  // Same-direction motion at identical speed produces no contact.
  sphere.start = Sphere{Vector3(1, 0, 0), 0.3f};
  sphere.end = Sphere{Vector3(2, 0, 0), 0.3f};
  const BallState chasing = BallAt(Vector3(0, 0, 0), Vector3(100, 0, 0));
  REQUIRE_FALSE(SweepBall(chasing, sphere, 0.01f, 0.11f).has_value());
}

TEST_CASE("sphere-capsule CCD detects a graze and rejects a miss",
          "[ball][collision][ccd]") {
  ColliderMotion capsule;
  capsule.id = 4;
  capsule.start = Capsule{Vector3(0, 0, 0), Vector3(0, 0, 2), 0.05f};
  capsule.end = capsule.start;

  // Grazing path passes within the sum of the radii.
  const BallState graze = BallAt(Vector3(-1, 0.1f, 1), Vector3(100, 0, 0));
  const auto hit = SweepBall(graze, capsule, 0.01f, 0.11f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->collider == 4);
  REQUIRE(hit->toi > 0.0f);
  REQUIRE(hit->toi < 1.0f);

  // A wider path never comes within reach.
  const BallState miss = BallAt(Vector3(-1, 0.3f, 1), Vector3(100, 0, 0));
  REQUIRE_FALSE(SweepBall(miss, capsule, 0.01f, 0.11f).has_value());
}

TEST_CASE("initial penetration reports time zero rather than tunneling",
          "[ball][collision][ccd]") {
  ColliderMotion sphere = StaticSphere(9, Vector3(0.1f, 0, 0), 0.3f);
  const BallState ball = BallAt(Vector3(0, 0, 0), Vector3(0, 0, 0));
  const auto hit = SweepBall(ball, sphere, 0.01f, 0.11f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->toi == 0.0f);
}

TEST_CASE("first contact picks the earliest hit and ties by collider id",
          "[ball][collision][ccd]") {
  const BallState ball = BallAt(Vector3(0, 0, 0), Vector3(100, 0, 0));

  {
    std::vector<ColliderMotion> colliders;
    ColliderMotion near = StaticSphere(5, Vector3(1, 0, 0), 0.1f);
    ColliderMotion far = StaticSphere(6, Vector3(2, 0, 0), 0.1f);
    colliders.push_back(far);
    colliders.push_back(near);
    const auto first = FirstContact(ball, colliders, 0.01f, 0.11f);
    REQUIRE(first.has_value());
    REQUIRE(first->collider == 5);
  }

  {
    // Simultaneous hits: two colliders on the same path and distance, so the
    // lower id wins regardless of input order.
    std::vector<ColliderMotion> colliders;
    ColliderMotion a = StaticSphere(12, Vector3(0, 0, -1), 0.05f);
    ColliderMotion b = StaticSphere(7, Vector3(0, 0, -1), 0.05f);
    colliders.push_back(a);
    colliders.push_back(b);
    const BallState moving = BallAt(Vector3(0, 0, 0), Vector3(0, 0, -100));
    const auto first = FirstContact(moving, colliders, 0.01f, 0.11f);
    REQUIRE(first.has_value());
    REQUIRE(first->collider == 7);
    REQUIRE(first.has_value());
    REQUIRE(first->collider == 7);
  }
}

TEST_CASE("first contact is deterministic and order independent",
          "[ball][collision][ccd]") {
  const BallState ball = BallAt(Vector3(0, 0, 0), Vector3(80, 0, 0));
  std::vector<ColliderMotion> order_a;
  order_a.push_back(StaticSphere(5, Vector3(1.5f, 0.1f, 0), 0.2f));
  order_a.push_back(StaticSphere(3, Vector3(1.0f, -0.1f, 0), 0.2f));
  order_a.push_back(StaticSphere(9, Vector3(2.0f, 0, 0), 0.2f));

  std::vector<ColliderMotion> order_b{order_a[2], order_a[0], order_a[1]};

  const auto first = FirstContact(ball, order_a, 0.01f, 0.11f);
  const auto second = FirstContact(ball, order_b, 0.01f, 0.11f);
  const auto third = FirstContact(ball, order_a, 0.01f, 0.11f);

  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(first->collider == second->collider);
  REQUIRE(std::fabs(first->toi - second->toi) < 1e-6f);
  REQUIRE(third->collider == first->collider);
  REQUIRE(third->toi == first->toi);
}
TEST_CASE("advance ball integrates free motion without contacts", "[ball][collision][advance]") {
  const BallState ball = BallAt(Vector3(0, 0, 0), Vector3(10, 0, 0));
  std::vector<ColliderMotion> none;
  const auto result = football::ball::AdvanceBall(ball, none, 0.1f, 0.11f, 0.5f);
  REQUIRE(result.contacts.empty());
  REQUIRE(result.state.velocity.coords[0] == 10.0f);
  REQUIRE(std::fabs(result.state.position.coords[0] - 1.0f) < 1e-5f);
}

TEST_CASE("advance ball reflects off a plane", "[ball][collision][advance]") {
  ColliderMotion ground;
  ground.id = 1;
  ground.start = Plane{Vector3(0, 0, 0), Vector3(0, 0, 1)};
  ground.end = ground.start;
  const std::vector<ColliderMotion> colliders{ground};
  const BallState ball = BallAt(Vector3(0, 0, 1), Vector3(0, 0, -50));
  const auto result = football::ball::AdvanceBall(ball, colliders, 0.05f, 0.11f, 0.5f);
  REQUIRE(result.contacts.size() == 1);
  REQUIRE(result.state.velocity.coords[2] > 0.0f);
  REQUIRE(result.state.position.coords[2] > 0.11f - 1e-4f);
}

TEST_CASE("advance ball rests on the ground without oscillation", "[ball][collision][advance]") {
  ColliderMotion ground;
  ground.id = 2;
  ground.start = Plane{Vector3(0, 0, 0), Vector3(0, 0, 1)};
  ground.end = ground.start;
  const std::vector<ColliderMotion> colliders{ground};
  const BallState ball = BallAt(Vector3(0, 0, 0.10f), Vector3(0, 0, 0));
  const auto result = football::ball::AdvanceBall(ball, colliders, 0.01f, 0.11f, 0.5f);
  REQUIRE(result.contacts.size() == 1);
  REQUIRE(std::fabs(result.state.velocity.coords[2]) < 1e-5f);
  REQUIRE(std::fabs(result.state.position.coords[2] - 0.10f) < 1e-5f);
}

TEST_CASE("advance ball resolves two contacts in one tick", "[ball][collision][advance]") {
  ColliderMotion right;
  right.id = 1;
  right.start = Plane{Vector3(1, 0, 0), Vector3(-1, 0, 0)};
  right.end = right.start;
  ColliderMotion left;
  left.id = 2;
  left.start = Plane{Vector3(-1, 0, 0), Vector3(1, 0, 0)};
  left.end = left.start;
  const std::vector<ColliderMotion> colliders{right, left};
  const BallState ball = BallAt(Vector3(0, 0, 0), Vector3(100, 0, 0));
  const auto result = football::ball::AdvanceBall(ball, colliders, 0.1f, 0.11f, 1.0f);
  REQUIRE(result.contacts.size() >= 2);
}

TEST_CASE("pitch colliders build a fixed deterministic static world",
          "[ball][collision][pitch]") {
  using football::ball::BuildPitchColliders;
  const football::model::Pitch pitch;
  const football::model::BallConfig ball;
  const auto world = BuildPitchColliders(pitch, ball);
  const auto again = BuildPitchColliders(pitch, ball);

  REQUIRE(world.size() == 7);
  REQUIRE(again.size() == 7);
  for (std::size_t i = 0; i < world.size(); ++i) {
    REQUIRE(world[i].id == i + 1);
    REQUIRE(again[i].id == world[i].id);
    REQUIRE(again[i].start.index() == world[i].start.index());
  }

  // Ground plane with +z normal.
  const auto* ground = std::get_if<Plane>(&world[0].start);
  REQUIRE(ground != nullptr);
  REQUIRE(ground->normal.coords[2] > 0.0f);

  // Posts are vertical capsules at the goal line; crossbars horizontal.
  for (std::size_t i = 1; i < world.size(); ++i) {
    REQUIRE(std::holds_alternative<Capsule>(world[i].start));
  }
  const auto& bar = std::get<Capsule>(world[6].start);
  REQUIRE(bar.a.coords[2] == bar.b.coords[2]);
  REQUIRE(bar.a.coords[2] == pitch.goal_height());
}

TEST_CASE("new kernel detects a post and crossbar hit", "[ball][collision][pitch]") {
  using football::ball::BuildPitchColliders;
  const football::model::Pitch pitch;
  const football::model::BallConfig ball;
  const auto world = BuildPitchColliders(pitch, ball);

  // Right high post (id 5) at (+half_length, +goal_half_width).
  const float x = pitch.half_length();
  const float y = pitch.goal_half_width();
  const BallState at_post = BallAt(Vector3(x - 1, y, 0.5f), Vector3(200, 0, 0));
  const auto post_hit = SweepBall(at_post, world[4], 0.01f, ball.radius());
  REQUIRE(post_hit.has_value());
  REQUIRE(post_hit->collider == 5);

  // Left crossbar (id 6) at (-half_length, z = goal_height).
  const BallState at_bar = BallAt(Vector3(-x + 1, 0, pitch.goal_height() - 0.1f),
                                  Vector3(-200, 0, 0));
  const auto bar_hit = SweepBall(at_bar, world[5], 0.01f, ball.radius());
  REQUIRE(bar_hit.has_value());
  REQUIRE(bar_hit->collider == 6);
}

TEST_CASE("new kernel keeps a tangentially rolling ball moving",
          "[ball][collision][pitch]") {
  using football::ball::BuildPitchColliders;
  const football::model::Pitch pitch;
  const football::model::BallConfig ball;
  const auto world = BuildPitchColliders(pitch, ball);

  const BallState rolling = BallAt(Vector3(0, 0, ball.radius()), Vector3(5, 0, 0));
  const auto result = football::ball::AdvanceBall(rolling, world, 0.01f, ball.radius(),
                                                  ball.restitution());
  REQUIRE(result.state.position.coords[0] > 0.0f);
  REQUIRE(result.state.velocity.coords[0] == 5.0f);
}
