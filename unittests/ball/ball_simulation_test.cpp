// Ball one-tick physics: free motion, drag, spin, grass rolling, landing and
// the Ball::Step/Predict lifecycle. Explicit dynamics keep each rule isolated.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>
#include <vector>

#include "football/ball/ball.hpp"
#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_model.hpp"
#include "football/ball/ball_state.hpp"
#include "football/ball/ball_world.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/geometry/collider.hpp"

using blunted::Quaternion;
using blunted::Vector3;
using football::ball::AdvanceBallTick;
using football::ball::Ball;
using football::ball::BallDynamics;
using football::ball::BallEndpointConstraint;
using football::ball::BallImpulse;
using football::ball::BallModel;
using football::ball::BallState;
using football::ball::BallTickInput;
using football::ball::BallWorld;
using football::geometry::ColliderMotion;
using football::geometry::ContactMaterial;
using football::geometry::Plane;
using Catch::Approx;

namespace {

constexpr float kDt = football::sim::kTickSeconds;

BallState BallAt(const Vector3& position, const Vector3& velocity) {
  BallState state;
  state.position = position;
  state.velocity = velocity;
  return state;
}

// Gravity only: no air drag, no ground resistance, no spin coupling.
BallDynamics AirDynamics() {
  BallDynamics dynamics;
  dynamics.gravity = 9.81f;
  dynamics.quadratic_resistance = 0.0f;
  dynamics.ground_deceleration = 0.0f;
  dynamics.quadratic_ground_resistance = 0.0f;
  dynamics.grass_height = 0.025f;
  dynamics.spin_decay = 0.0f;
  dynamics.magnus_coefficient = 0.0f;
  return dynamics;
}

ColliderMotion GroundPlane(float restitution) {
  const Plane plane{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
  return ColliderMotion{1, plane, plane, ContactMaterial{restitution, 0.0f}};
}

BallWorld WorldWithGround(float restitution) {
  BallWorld world;
  world.static_colliders.push_back(GroundPlane(restitution));
  return world;
}

}  // namespace

TEST_CASE("Free fall follows gravity", "[unit][ball][simulation]") {
  const BallModel config;
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});

  const auto result = AdvanceBallTick(initial, {}, kDt, config, AirDynamics());

  CHECK(result.state.velocity.coords[2] == Approx(-9.81f * kDt).margin(1e-4f));
  CHECK(result.state.position.coords[2] ==
        Approx(10.0f - 0.5f * 9.81f * kDt * kDt).margin(1e-4f));
  CHECK(result.contacts.empty());
}

TEST_CASE("Horizontal flight without drag keeps velocity",
          "[unit][ball][simulation]") {
  const BallModel config;
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 0.0f});
  BallDynamics dynamics = AirDynamics();
  dynamics.gravity = 0.0f;

  const auto result = AdvanceBallTick(initial, {}, kDt, config, dynamics);

  CHECK(result.state.velocity.coords[0] == Approx(5.0f).margin(1e-6f));
  CHECK(result.state.velocity.coords[2] == Approx(0.0f).margin(1e-6f));
  CHECK(result.state.position.coords[0] == Approx(5.0f * kDt).margin(1e-6f));
}

TEST_CASE("Air drag removes kinetic energy", "[unit][ball][simulation]") {
  const BallModel config;
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {10.0f, 0.0f, 0.0f});
  BallDynamics dynamics = AirDynamics();
  dynamics.quadratic_resistance = 0.02f;

  const auto result = AdvanceBallTick(initial, {}, kDt, config, dynamics);

  CHECK(result.state.velocity.GetLength() < 10.0f);
  CHECK(result.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Spin bends a moving ball sideways (Magnus)", "[unit][ball][simulation]") {
  const BallModel config;
  BallState initial = BallAt({0.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 0.0f});
  initial.angular_velocity = Vector3(0.0f, 0.0f, 10.0f);
  BallDynamics dynamics = AirDynamics();
  dynamics.magnus_coefficient = 0.05f;

  const auto result = AdvanceBallTick(initial, {}, kDt, config, dynamics);

  CHECK(result.state.velocity.coords[1] > 0.0f);
  CHECK(result.state.velocity.coords[1] ==
        Approx(0.05f * 50.0f * kDt).margin(1e-4f));
}

TEST_CASE("Grass rolling decelerates horizontal speed", "[unit][ball][simulation]") {
  const BallModel config;
  const auto initial = BallAt({0.0f, 0.0f, config.radius()}, {5.0f, 0.0f, 0.0f});
  BallDynamics dynamics = AirDynamics();
  dynamics.ground_deceleration = 1.6f;

  const auto result = AdvanceBallTick(initial, {}, kDt, config, dynamics);

  CHECK(result.state.position.coords[2] == Approx(config.radius()));
  CHECK(result.state.velocity.coords[0] < 5.0f);
  CHECK(result.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Landing on the ground reflects by the contact restitution",
          "[unit][ball][simulation]") {
  const BallModel config;
  const auto initial = BallAt({0.0f, 0.0f, 0.15f}, {0.0f, 0.0f, -5.0f});
  const std::array<ColliderMotion, 1> colliders{GroundPlane(0.5f)};

  const auto result = AdvanceBallTick(initial, colliders, kDt, config, AirDynamics());

  REQUIRE(!result.contacts.empty());
  CHECK(result.state.velocity.coords[2] > 0.0f);
  CHECK(result.state.position.coords[2] == Approx(config.radius()).margin(1e-3f));
}

TEST_CASE("Initial penetration is repaired without an impulse",
          "[unit][ball][simulation]") {
  const BallModel config;
  const auto initial = BallAt({0.0f, 0.0f, 0.05f}, {1.0f, 0.0f, 0.0f});
  const std::array<ColliderMotion, 1> colliders{GroundPlane(0.5f)};

  const auto result = AdvanceBallTick(initial, colliders, kDt, config, AirDynamics());

  CHECK(result.state.position.coords[2] == Approx(config.radius()).margin(1e-4f));
  for (const auto& contact : result.contacts) {
    CHECK(contact.normal_impulse == Approx(0.0f));
  }
}

TEST_CASE("Ball Predict previews without mutating state",
          "[unit][ball][simulation]") {
  const BallModel config;
  Ball ball(config, BallWorld{});
  ball.Reset(BallAt({0.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 0.0f}));
  const BallState before = ball.state();

  const BallTickInput input;
  const auto preview = ball.Predict(input);

  CHECK(ball.state().position.coords[0] == Approx(before.position.coords[0]));
  CHECK(ball.state().velocity.coords[2] == Approx(before.velocity.coords[2]));
  CHECK(preview.state.position.coords[0] != Approx(before.position.coords[0]));
}

TEST_CASE("Ball Step commits exactly what Predict previewed",
          "[unit][ball][simulation]") {
  const BallModel config;
  Ball preview_ball(config, BallWorld{});
  Ball commit_ball(config, BallWorld{});
  const auto initial = BallAt({0.0f, 0.0f, 5.0f}, {2.0f, 0.0f, 0.0f});
  preview_ball.Reset(initial);
  commit_ball.Reset(initial);

  const BallTickInput input;
  const auto preview = preview_ball.Predict(input);
  const auto committed = commit_ball.Step(input);

  CHECK(preview.state.position.coords[0] == Approx(committed.state.position.coords[0]));
  CHECK(preview.state.position.coords[2] == Approx(committed.state.position.coords[2]));
  CHECK(preview.state.velocity.coords[2] == Approx(committed.state.velocity.coords[2]));
  CHECK(commit_ball.state().position.coords[0] ==
        Approx(committed.state.position.coords[0]));
}

TEST_CASE("Ball Predict over a horizon does not mutate state",
          "[unit][ball][simulation]") {
  const BallModel config;
  Ball ball(config, BallWorld{});
  ball.Reset(BallAt({0.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 0.0f}));
  const BallState before = ball.state();

  const BallState future =
      ball.Predict(football::sim::TickSpan{10}, football::ball::BallEnvironment{});

  CHECK(ball.state().position.coords[0] == Approx(before.position.coords[0]));
  CHECK(future.position.coords[2] < before.position.coords[2]);
}

TEST_CASE("Ball Reset restores the exact state", "[unit][ball][simulation]") {
  const BallModel config;
  Ball ball(config, BallWorld{});
  BallState state = BallAt({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
  state.angular_velocity = Vector3(7.0f, 8.0f, 9.0f);

  ball.Reset(state);

  CHECK(ball.state().position.coords[0] == Approx(1.0f));
  CHECK(ball.state().position.coords[2] == Approx(3.0f));
  CHECK(ball.state().velocity.coords[1] == Approx(5.0f));
  CHECK(ball.state().angular_velocity.coords[2] == Approx(9.0f));
}

TEST_CASE("Ball Mirror flips the coordinate frame", "[unit][ball][simulation]") {
  const BallModel config;
  Ball ball(config, BallWorld{});
  BallState state = BallAt({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
  state.angular_velocity = Vector3(7.0f, 8.0f, 9.0f);
  ball.Reset(state);

  ball.Mirror();

  CHECK(ball.state().position.coords[0] == Approx(-1.0f));
  CHECK(ball.state().position.coords[1] == Approx(-2.0f));
  CHECK(ball.state().position.coords[2] == Approx(3.0f));
  CHECK(ball.state().velocity.coords[0] == Approx(-4.0f));
  CHECK(ball.state().velocity.coords[1] == Approx(-5.0f));
  CHECK(ball.state().velocity.coords[2] == Approx(6.0f));
  CHECK(ball.state().angular_velocity.coords[0] == Approx(-7.0f));
  CHECK(ball.state().angular_velocity.coords[1] == Approx(-8.0f));
  CHECK(ball.state().angular_velocity.coords[2] == Approx(9.0f));
}

TEST_CASE("Active impulse changes velocity and spin only at the tick endpoint",
          "[unit][ball][simulation]") {
  const BallModel config;
  Ball ball(config, BallWorld{});
  ball.Reset(BallAt({0.0f, 0.0f, 10.0f}, {1.0f, 0.0f, 0.0f}));

  BallTickInput passive;
  const auto passive_preview = ball.Predict(passive);

  BallTickInput with_impulse;
  with_impulse.active_impulse =
      BallImpulse{Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 10.0f)};
  const auto impulse_preview = ball.Predict(with_impulse);

  CHECK(impulse_preview.active_impulse.has_value());
  CHECK(impulse_preview.state.position.coords[0] ==
        Approx(passive_preview.state.position.coords[0]).margin(1e-6f));
  CHECK(impulse_preview.state.position.coords[2] ==
        Approx(passive_preview.state.position.coords[2]).margin(1e-6f));
  CHECK(impulse_preview.state.velocity.coords[0] ==
        Approx(passive_preview.state.velocity.coords[0] + 1.0f / config.mass()).margin(1e-4f));
}

TEST_CASE("Endpoint constraint sets position/velocity and clears spin",
          "[unit][ball][simulation]") {
  const BallModel config;
  Ball ball(config, BallWorld{});
  ball.Reset(BallAt({0.0f, 0.0f, 10.0f}, {1.0f, 0.0f, 0.0f}));

  BallTickInput input;
  input.endpoint_constraint =
      BallEndpointConstraint{Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f)};
  const auto result = ball.Step(input);

  CHECK(result.endpoint_constraint.has_value());
  CHECK(result.state.position.coords[0] == Approx(1.0f));
  CHECK(result.state.position.coords[2] == Approx(3.0f));
  CHECK(result.state.velocity.coords[2] == Approx(6.0f));
  CHECK(result.state.angular_velocity.GetLength() == Approx(0.0f));
}

TEST_CASE("Ball rejects illegal dynamic collider ids", "[unit][ball][simulation]") {
  const BallModel config;
  const Plane plane{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
  const ColliderMotion dynamic_collider{1, plane, plane, ContactMaterial{0.5f, 0.0f}};

  SECTION("zero id") {
    Ball ball(config, BallWorld{});
    const std::vector<ColliderMotion> colliders{
        ColliderMotion{0, plane, plane, ContactMaterial{0.5f, 0.0f}}};
    BallTickInput input;
    input.dynamic_colliders = colliders;
    REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
  }

  SECTION("conflict with a static id") {
    Ball ball(config, WorldWithGround(0.5f));
    const std::vector<ColliderMotion> colliders{dynamic_collider};
    BallTickInput input;
    input.dynamic_colliders = colliders;
    REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
  }

  SECTION("duplicate dynamic ids") {
    Ball ball(config, BallWorld{});
    const ColliderMotion other{2, plane, plane, ContactMaterial{0.5f, 0.0f}};
    const std::vector<ColliderMotion> colliders{other, other};
    BallTickInput input;
    input.dynamic_colliders = colliders;
    REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
  }
}

TEST_CASE("Ball rejects simultaneous impulse and endpoint constraint",
          "[unit][ball][simulation]") {
  const BallModel config;
  Ball ball(config, BallWorld{});
  BallTickInput input;
  input.active_impulse = BallImpulse{Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)};
  input.endpoint_constraint =
      BallEndpointConstraint{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)};
  REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
}
