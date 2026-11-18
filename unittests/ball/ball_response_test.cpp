// Ball impulse response: analytic invariants exercised through Ball members.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "football/ball/ball.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/vector3.hpp"

using football::math::Vector3;
using football::ball::Ball;
using football::ball::BallModel;
using football::ball::BallState;
using football::geometry::ContactMaterial;
using Catch::Approx;

namespace {

BallModel TestBallModel() { return BallModel{0.5f, 0.11f, 0.02f}; }

BallState BallAt(const Vector3& position, const Vector3& velocity) {
  BallState state;
  state.position = position;
  state.velocity = velocity;
  return state;
}

// The shell point facing the surface, mirroring how a sweep reports a contact.
Vector3 ContactPoint(const Ball& ball, const Vector3& normal) {
  return ball.get_state().position - normal * ball.model().radius;
}

Vector3 SolveAndApply(Ball& ball, const Vector3& normal, const Vector3& point,
                      const ContactMaterial& material) {
  const auto impulse =
      ball.SolveContactImpulse(normal, point, Vector3(0, 0, 0), material);
  REQUIRE(impulse.has_value());
  ball.ApplyImpulseAtPoint(*impulse, point);
  return *impulse;
}

// The normal share of an impulse; the rest is tangential.
float NormalShare(const Vector3& impulse, const Vector3& normal) {
  return impulse.GetDotProduct(normal);
}

Vector3 TangentialShare(const Vector3& impulse, const Vector3& normal) {
  return impulse - normal * NormalShare(impulse, normal);
}

}  // namespace

TEST_CASE("Impulse response uses thin-shell sphere inertia", "[unit][ball][response]") {
  Ball ball(TestBallModel());
  const float expected = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  ball.ApplyImpulseAtPoint({1, 0, 0}, {0, 0, -0.11f});
  const float inferred = -0.11f / ball.get_state().angular_velocity.coords[1];
  CHECK(inferred == Approx(expected).margin(1e-9f));
}

TEST_CASE("A radial impulse changes linear velocity by J/m only",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  const auto before = ball.get_state();
  // Striking the bottom straight down: the lever arm is parallel to the impulse,
  // so the change is torque-free translation.
  ball.ApplyImpulseAtPoint({0, 0, -1}, {0, 0, -ball.model().radius});
  const auto next = ball.get_state();

  CHECK(next.velocity.coords[0] == Approx(0.0f).margin(1e-6f));
  CHECK(next.velocity.coords[1] == Approx(0.0f).margin(1e-6f));
  CHECK(next.velocity.coords[2] == Approx(-2.0f).margin(1e-5f));
  CHECK(next.angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
  CHECK(next.position == before.position);
  CHECK_FALSE(next.orientation != before.orientation);
}

TEST_CASE("Offset impulse produces the expected spin direction",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  // +x impulse at the bottom gives angular impulse about -y.
  ball.ApplyImpulseAtPoint({1, 0, 0}, {0, 0, -0.11f});
  const auto next = ball.get_state();

  CHECK(next.velocity.coords[0] == Approx(2.0f).margin(1e-5f));
  const float expected_inertia = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  CHECK(next.angular_velocity.coords[1] ==
        Approx(-0.11f / expected_inertia).margin(1e-5f));
  CHECK(next.angular_velocity.coords[0] == Approx(0.0f).margin(1e-6f));
  CHECK(next.angular_velocity.coords[2] == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("The lever arm direction comes from the point on the shell",
          "[unit][ball][response]") {
  const Ball ball_model(TestBallModel());
  const float radius = ball_model.model().radius;

  // Same impulse magnitude, two surface points: the lever arm points along the
  // line from the centre to the strike point, so the spin axis follows it.
  Ball bottom(TestBallModel());
  bottom.ApplyImpulseAtPoint({1, 0, 0}, {0, 0, -radius});
  const auto from_bottom = bottom.get_state();

  Ball side(TestBallModel());
  side.ApplyImpulseAtPoint({1, 0, 0}, {radius, 0, 0});
  const auto from_side = side.get_state();

  CHECK(from_bottom.velocity.coords[0] == Approx(from_side.velocity.coords[0]));
  // Striking the bottom spins about -y; striking the +x side produces no torque
  // at all because there the lever arm is parallel to the impulse.
  CHECK(from_bottom.angular_velocity.coords[1] < 0.0f);
  CHECK(from_side.angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Frictionless contact reflects the normal velocity by restitution",
          "[unit][ball][response]") {
  for (const float restitution : {0.0f, 0.5f, 1.0f}) {
    INFO("restitution=" << restitution);
    Ball ball(TestBallModel());
    ball.set_state(BallAt({0, 0, 0.11f}, {0, 0, -2}));
    const Vector3 normal{0, 0, 1};
    const Vector3 point = ContactPoint(ball, normal);
    const Vector3 impulse = SolveAndApply(ball, normal, point, {restitution, 0});

    CHECK(NormalShare(impulse, normal) > 0.0f);
    CHECK(ball.get_state().velocity.coords[2] == Approx(2.0f * restitution).margin(1e-4f));
    CHECK(ball.get_state().angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
    CHECK(TangentialShare(impulse, normal).GetLength() == Approx(0.0f).margin(1e-6f));
  }
}

TEST_CASE("Separating contact produces no impulse at all",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {0, 0, 2}));
  const auto impulse = ball.SolveContactImpulse(
      {0, 0, 1}, ContactPoint(ball, {0, 0, 1}), Vector3(0, 0, 0), {0.5f, 0});

  CHECK_FALSE(impulse.has_value());
  CHECK(ball.get_state().velocity.coords[2] == Approx(2.0f));
  CHECK(ball.get_state().velocity.coords[0] == Approx(0.0f));
}

TEST_CASE("Tangential impulse is Coulomb-capped by friction",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {3, 0, -2}));
  const ContactMaterial material{0.5f, 0.2f};
  const Vector3 normal{0, 0, 1};
  const Vector3 point = ContactPoint(ball, normal);
  const Vector3 impulse = SolveAndApply(ball, normal, point, material);

  REQUIRE(NormalShare(impulse, normal) > 0.0f);
  CHECK(TangentialShare(impulse, normal).GetLength() <=
        material.friction * NormalShare(impulse, normal) + 1e-5f);
}

TEST_CASE("Friction slows tangential velocity and produces spin",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {3, 0, -2}));
  const Vector3 normal{0, 0, 1};
  const Vector3 point = ContactPoint(ball, normal);
  const Vector3 impulse = SolveAndApply(ball, normal, point, {0.5f, 0.5f});

  CHECK(TangentialShare(impulse, normal).GetLength() > 0.0f);
  CHECK(ball.get_state().velocity.coords[0] < 3.0f);
  CHECK(ball.get_state().angular_velocity.GetLength() > 0.0f);
}

TEST_CASE("Contact resolution takes its lever arm from the contact point",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {0, 0, -2}));
  const auto initial = ball.get_state();
  const ContactMaterial frictionless{0.5f, 0};
  const Vector3 normal{0, 0, 1};

  SolveAndApply(ball, normal, ContactPoint(ball, normal), frictionless);
  CHECK(ball.get_state().angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));

  ball.set_state(initial);
  // Same normal and velocity, but the recorded point sits on the +x flank:
  // the lever arm therefore follows the point, not the -normal * radius rule.
  const Vector3 offset_point = initial.position + Vector3(ball.model().radius, 0, 0);
  const Vector3 impulse = SolveAndApply(ball, normal, offset_point, frictionless);
  const float expected_inertia = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  CHECK(ball.get_state().angular_velocity.coords[1] ==
        Approx(-ball.model().radius * NormalShare(impulse, normal) / expected_inertia)
            .margin(1e-5f));
}

TEST_CASE("Moving collider velocity enters the relative contact velocity",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {0, 0, 1}));
  const Vector3 point = ContactPoint(ball, {0, 0, 1});

  CHECK_FALSE(ball.SolveContactImpulse({0, 0, 1}, point, Vector3(0, 0, 0), {0.5f, 0})
                  .has_value());
  CHECK(ball.SolveContactImpulse({0, 0, 1}, point, Vector3(0, 0, 3), {0.5f, 0})
            .has_value());
}

TEST_CASE("Solving uses current state without applying the impulse",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({1, 2, 0.11f}, {0, 0, -2}));
  const Vector3 point = ContactPoint(ball, {0, 0, 1});
  const auto before = ball.get_state();
  const auto impulse =
      ball.SolveContactImpulse({0, 0, 1}, point, Vector3(0, 0, 0), {0.5f, 0});
  REQUIRE(impulse.has_value());
  // Frictionless, so the whole impulse points along the normal.
  CHECK(impulse->coords[2] == Approx(1.5f));
  CHECK(ball.get_state().position == before.position);
  CHECK(ball.get_state().velocity == before.velocity);
  CHECK(ball.get_state().angular_velocity == before.angular_velocity);
  CHECK_FALSE(ball.get_state().orientation != before.orientation);

  ball.ApplyImpulseAtPoint(*impulse, point);
  CHECK_FALSE(ball.SolveContactImpulse({0, 0, 1}, point, Vector3(0, 0, 0), {0.5f, 0})
                  .has_value());
}

TEST_CASE("Successive impulses accumulate on the current ball state",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  auto initial = BallAt({4, 5, 6}, {1, 2, 3});
  initial.angular_velocity = Vector3(0, 2, 0);
  initial.orientation.SetAngleAxis(0.4f, {0, 0, 1});
  ball.set_state(initial);
  const auto point = initial.position + Vector3(0, 0, -ball.model().radius);

  ball.ApplyImpulseAtPoint({1, 0, 0}, point);
  ball.ApplyImpulseAtPoint({0.5f, 0, 0}, point);
  const auto after = ball.get_state();
  CHECK(after.velocity.coords[0] == Approx(4.0f));
  CHECK(after.velocity.coords[1] == Approx(2.0f));
  CHECK(after.velocity.coords[2] == Approx(3.0f));
  const float arm = point.coords[2] - initial.position.coords[2];
  const float expected_inertia = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  CHECK(after.angular_velocity.coords[1] ==
        Approx(2.0f + arm * 1.5f / expected_inertia).margin(1e-5f));
  CHECK(after.position == initial.position);
  CHECK_FALSE(after.orientation != initial.orientation);
}