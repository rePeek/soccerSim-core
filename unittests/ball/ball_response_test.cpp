// Ball response: momentum transfer (linear and angular) and the contact
// restitution/friction rules. Uses analytic invariants, not stored traces.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_model.hpp"
#include "football/ball/ball_response.hpp"
#include "football/ball/ball_state.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/geometry/collider.hpp"

using blunted::Vector3;
using football::ball::ApplyImpulseAtPoint;
using football::ball::BallContact;
using football::ball::BallInertia;
using football::ball::BallModel;
using football::ball::BallState;
using football::ball::ResolveContactResponse;
using football::geometry::ContactMaterial;
using Catch::Approx;

TEST_CASE("BallInertia uses the thin-shell sphere factor", "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  const float expected = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  CHECK(BallInertia(config) == Approx(expected).margin(1e-9f));
}

TEST_CASE("Central impulse changes linear velocity by J/m only",
          "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  BallState ball;
  ball.position = Vector3(0.0f, 0.0f, 0.0f);

  const auto next =
      ApplyImpulseAtPoint(ball, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), config);

  CHECK(next.velocity.coords[0] == Approx(2.0f).margin(1e-5f));
  CHECK(next.velocity.coords[1] == Approx(0.0f));
  CHECK(next.velocity.coords[2] == Approx(0.0f));
  CHECK(next.angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
  CHECK(next.position.coords[0] == Approx(ball.position.coords[0]));
  CHECK(next.position.coords[1] == Approx(ball.position.coords[1]));
  CHECK(next.position.coords[2] == Approx(ball.position.coords[2]));
}

TEST_CASE("Offset impulse produces the expected spin direction",
          "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  BallState ball;
  ball.position = Vector3(0.0f, 0.0f, 0.0f);

  // Force +x applied at the bottom surface (r = -R z) gives torque r x J = -y.
  const auto next = ApplyImpulseAtPoint(
      ball, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -0.11f), config);

  CHECK(next.velocity.coords[0] == Approx(2.0f).margin(1e-5f));
  CHECK(next.angular_velocity.coords[1] < 0.0f);
  CHECK(next.angular_velocity.coords[0] == Approx(0.0f).margin(1e-6f));
  CHECK(next.angular_velocity.coords[2] == Approx(0.0f).margin(1e-6f));
}

namespace {

BallState BallAt(const Vector3& position, const Vector3& velocity) {
  BallState state;
  state.position = position;
  state.velocity = velocity;
  return state;
}

BallContact ContactWithNormal(const Vector3& normal) {
  BallContact contact;
  contact.collider = 1;
  contact.toi = 0.5f;
  contact.normal = normal;
  contact.point = Vector3(0.0f, 0.0f, 0.0f);
  return contact;
}

}  // namespace

TEST_CASE("Frictionless contact reflects the normal velocity by restitution",
          "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {0.0f, 0.0f, -2.0f});
  const auto response = ResolveContactResponse(
      ball, ContactWithNormal({0.0f, 0.0f, 1.0f}), ContactMaterial{0.5f, 0.0f},
      Vector3(0.0f, 0.0f, 0.0f), config);

  CHECK(response.normal_impulse > 0.0f);
  CHECK(response.state.velocity.coords[2] == Approx(1.0f).margin(1e-4f));
  CHECK(response.state.angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
  CHECK(response.tangential_impulse.GetLength() == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Separating contact produces no new normal impulse",
          "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {0.0f, 0.0f, 2.0f});
  const auto response = ResolveContactResponse(
      ball, ContactWithNormal({0.0f, 0.0f, 1.0f}), ContactMaterial{0.5f, 0.0f},
      Vector3(0.0f, 0.0f, 0.0f), config);

  CHECK(response.normal_impulse == Approx(0.0f));
  CHECK(response.state.velocity.coords[2] == Approx(2.0f));
  CHECK(response.state.velocity.coords[0] == Approx(0.0f));
}

TEST_CASE("Tangential impulse is Coulomb-capped by friction",
          "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {3.0f, 0.0f, -2.0f});
  const ContactMaterial material{0.5f, 0.2f};
  const auto response = ResolveContactResponse(
      ball, ContactWithNormal({0.0f, 0.0f, 1.0f}), material,
      Vector3(0.0f, 0.0f, 0.0f), config);

  REQUIRE(response.normal_impulse > 0.0f);
  CHECK(response.tangential_impulse.GetLength() <=
        material.friction * response.normal_impulse + 1e-5f);
}

TEST_CASE("Friction slows tangential velocity and produces spin",
          "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {3.0f, 0.0f, -2.0f});
  const auto response = ResolveContactResponse(
      ball, ContactWithNormal({0.0f, 0.0f, 1.0f}), ContactMaterial{0.5f, 0.5f},
      Vector3(0.0f, 0.0f, 0.0f), config);

  CHECK(response.tangential_impulse.GetLength() > 0.0f);
  CHECK(response.state.velocity.coords[0] < 3.0f);
  CHECK(response.state.angular_velocity.GetLength() > 0.0f);
}

TEST_CASE("Moving collider velocity enters the relative contact velocity",
          "[unit][ball][response]") {
  const BallModel config(0.5f, 0.11f);
  // Ball already moving away (+z) from a static surface: no impulse.
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {0.0f, 0.0f, 1.0f});
  const auto static_response = ResolveContactResponse(
      ball, ContactWithNormal({0.0f, 0.0f, 1.0f}), ContactMaterial{0.5f, 0.0f},
      Vector3(0.0f, 0.0f, 0.0f), config);
  CHECK(static_response.normal_impulse == Approx(0.0f));

  // The same ball meets a collider moving up faster than it: approaching again.
  const auto moving_response = ResolveContactResponse(
      ball, ContactWithNormal({0.0f, 0.0f, 1.0f}), ContactMaterial{0.5f, 0.0f},
      Vector3(0.0f, 0.0f, 3.0f), config);
  CHECK(moving_response.normal_impulse > 0.0f);
}
