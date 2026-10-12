// Ball one-tick physics: free motion, drag, spin, grass rolling, landing and
// the Ball::Step/Predict lifecycle.
//
// Long-lived parameters are supplied at construction through BallModel (the
// ball) and Pitch (gravity, ground, static colliders); each call only adds the
// colliders and interactions new in this tick.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>
#include <variant>
#include <vector>

#include "football/ball/ball.hpp"
#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_model.hpp"
#include "football/ball/ball_state.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/pitch/pitch.hpp"

using blunted::Vector3;
using football::ball::AdvanceBallTick;
using football::ball::Ball;
using football::ball::BallEndpointConstraint;
using football::ball::BallImpulse;
using football::ball::BallModel;
using football::ball::BallState;
using football::ball::BallTickInput;
using football::geometry::ColliderMotion;
using football::geometry::ContactMaterial;
using football::geometry::Plane;
using football::pitch::Pitch;
using Catch::Approx;

namespace {

constexpr float kDt = football::sim::kTickSeconds;

BallState BallAt(const Vector3& position, const Vector3& velocity) {
  BallState state;
  state.position = position;
  state.velocity = velocity;
  return state;
}

// A ball with no air drag and no Magnus coupling.
BallModel BallWithoutAero() { return BallModel(0.43f, 0.11f, 0.0f, 0.0f); }

// A pitch whose ground does not slow the ball, so gravity/spin tests are pure.
Pitch FrictionlessPitch() { return Pitch(105.0f, 68.0f, 0.0f, 0.0f, 0.025f); }

ColliderMotion GroundPlane(float restitution) {
  const Plane plane{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
  return ColliderMotion{100, plane, plane, ContactMaterial{restitution, 0.0f}};
}

}  // namespace

TEST_CASE("Free fall follows gravity", "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});

  const auto result = AdvanceBallTick(initial, {}, kDt, config, pitch);

  CHECK(result.state.velocity.coords[2] == Approx(-9.81f * kDt).margin(1e-4f));
  CHECK(result.state.position.coords[2] ==
        Approx(10.0f - 0.5f * 9.81f * kDt * kDt).margin(1e-4f));
  CHECK(result.contacts.empty());
}

TEST_CASE("Horizontal flight without drag keeps horizontal velocity",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 0.0f});

  const auto result = AdvanceBallTick(initial, {}, kDt, config, pitch);

  // Gravity changes only the vertical component.
  CHECK(result.state.velocity.coords[0] == Approx(5.0f).margin(1e-6f));
  CHECK(result.state.position.coords[0] == Approx(5.0f * kDt).margin(1e-6f));
}

TEST_CASE("Air drag removes kinetic energy", "[unit][ball][simulation]") {
  const BallModel config(0.43f, 0.11f, 0.02f, 0.0f);
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {10.0f, 0.0f, 0.0f});

  const auto result = AdvanceBallTick(initial, {}, kDt, config, pitch);

  CHECK(result.state.velocity.GetLength() < 10.0f);
  CHECK(result.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Spin bends a moving ball sideways (Magnus)", "[unit][ball][simulation]") {
  const BallModel config(0.43f, 0.11f, 0.0f, 0.05f);
  const Pitch pitch = FrictionlessPitch();
  BallState initial = BallAt({0.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 0.0f});
  initial.angular_velocity = Vector3(0.0f, 0.0f, 10.0f);

  const auto result = AdvanceBallTick(initial, {}, kDt, config, pitch);

  CHECK(result.state.velocity.coords[1] > 0.0f);
  CHECK(result.state.velocity.coords[1] == Approx(0.05f * 50.0f * kDt).margin(1e-4f));
}

TEST_CASE("Grass rolling decelerates horizontal speed", "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch;  // default ground deceleration (1.6 m/s^2)
  const auto initial = BallAt({0.0f, 0.0f, config.radius()}, {5.0f, 0.0f, 0.0f});

  const auto result = AdvanceBallTick(initial, {}, kDt, config, pitch);

  CHECK(result.state.position.coords[2] == Approx(config.radius()));
  CHECK(result.state.velocity.coords[0] < 5.0f);
  CHECK(result.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Landing on the ground reflects by the contact restitution",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 0.15f}, {0.0f, 0.0f, -5.0f});
  const std::array<ColliderMotion, 1> colliders{GroundPlane(0.5f)};

  const auto result = AdvanceBallTick(initial, colliders, kDt, config, pitch);

  REQUIRE(!result.contacts.empty());
  CHECK(result.state.velocity.coords[2] > 0.0f);
  CHECK(result.state.position.coords[2] == Approx(config.radius()).margin(1e-3f));
}

TEST_CASE("Initial penetration is repaired without an impulse",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 0.05f}, {1.0f, 0.0f, 0.0f});
  const std::array<ColliderMotion, 1> colliders{GroundPlane(0.5f)};

  const auto result = AdvanceBallTick(initial, colliders, kDt, config, pitch);

  CHECK(result.state.position.coords[2] == Approx(config.radius()).margin(1e-4f));
  for (const auto& contact : result.contacts) {
    CHECK(contact.normal_impulse == Approx(0.0f));
  }
}

TEST_CASE("The pitch scene carries the ground and goal colliders",
          "[unit][ball][simulation]") {
  const Pitch pitch;
  REQUIRE(pitch.colliders().size() == 7);
  // Grass is the single ground plane, built first.
  CHECK(std::holds_alternative<football::geometry::Plane>(pitch.colliders()[0].start));
  // Goal furniture is capsules (four posts + two crossbars).
  for (std::size_t i = 1; i < pitch.colliders().size(); ++i) {
    CHECK(std::holds_alternative<football::geometry::Capsule>(
        pitch.colliders()[i].start));
  }
  // Stable ids: ground 1, then the goal, in a fixed order.
  CHECK(pitch.colliders().front().id == 1);
  CHECK(pitch.colliders()[1].id == 2);
  CHECK(pitch.colliders().back().id == 7);
}

TEST_CASE("Ball Predict previews without mutating state",
          "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball ball(config, pitch);
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
  const Pitch pitch;
  Ball preview_ball(config, pitch);
  Ball commit_ball(config, pitch);
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
  const Pitch pitch;
  Ball ball(config, pitch);
  ball.Reset(BallAt({0.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 0.0f}));
  const BallState before = ball.state();

  const BallState future = ball.Predict(football::sim::TickSpan{10});

  CHECK(ball.state().position.coords[0] == Approx(before.position.coords[0]));
  CHECK(future.position.coords[2] < before.position.coords[2]);
}

TEST_CASE("Ball Reset restores the exact state", "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball ball(config, pitch);
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
  const Pitch pitch;
  Ball ball(config, pitch);
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
  const Pitch pitch;
  Ball ball(config, pitch);
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
  const Pitch pitch;
  Ball ball(config, pitch);
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
  const Pitch pitch;
  const Plane plane{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};

  SECTION("zero id") {
    Ball ball(config, pitch);
    const std::vector<ColliderMotion> colliders{
        ColliderMotion{0, plane, plane, ContactMaterial{0.5f, 0.0f}}};
    BallTickInput input;
    input.dynamic_colliders = colliders;
    REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
  }

  SECTION("conflict with a pitch collider id") {
    Ball ball(config, pitch);
    const std::vector<ColliderMotion> colliders{
        ColliderMotion{pitch.colliders().front().id, plane, plane,
                       ContactMaterial{0.5f, 0.0f}}};
    BallTickInput input;
    input.dynamic_colliders = colliders;
    REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
  }

  SECTION("duplicate dynamic ids") {
    Ball ball(config, pitch);
    const ColliderMotion other{100, plane, plane, ContactMaterial{0.5f, 0.0f}};
    const std::vector<ColliderMotion> colliders{other, other};
    BallTickInput input;
    input.dynamic_colliders = colliders;
    REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
  }
}

TEST_CASE("Ball rejects simultaneous impulse and endpoint constraint",
          "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball ball(config, pitch);
  BallTickInput input;
  input.active_impulse = BallImpulse{Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)};
  input.endpoint_constraint =
      BallEndpointConstraint{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)};
  REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
}
