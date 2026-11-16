// Ball response: momentum transfer (linear and angular) and the contact
// restitution/friction rules. Uses analytic invariants, not stored traces.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/ball/ball.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/vector3.hpp"

using football::math::Vector3;
using football::ball::ApplyImpulseAtPoint;
using football::ball::BallContact;
using football::ball::BallInertia;
using football::ball::BallModel;
using football::ball::BallState;
using football::ball::ContactImpulse;
using football::ball::SolveContactImpulse;
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

// A contact recorded the way the contact layer records it: the world point is
// the ball surface facing the collider.
BallContact SurfaceContact(const BallState& ball, const Vector3& normal,
                           float radius) {
  BallContact contact;
  contact.collider = 1;
  contact.toi = 0.5f;
  contact.normal = normal;
  contact.point = ball.position - normal * radius;
  return contact;
}

// The two calls a caller makes: solve the impulse, then apply it where it acts.
struct Resolved {
  BallState state;
  ContactImpulse impulse;
};

Resolved Resolve(const BallState& ball, const BallContact& contact,
                 const ContactMaterial& material, const BallModel& config) {
  const ContactImpulse solved = SolveContactImpulse(ball, contact, material, config);
  return {ApplyImpulseAtPoint(ball, solved.impulse, contact.point, config), solved};
}

// Tangential part of the applied impulse, recovered from the velocity change.
Vector3 TangentialImpulse(const BallState& before, const BallState& after,
                          const Vector3& normal, float normal_impulse,
                          const BallModel& config) {
  const Vector3 total = (after.velocity - before.velocity) * config.mass;
  return total - normal * normal_impulse;
}

}  // namespace

TEST_CASE("BallInertia uses the thin-shell sphere factor", "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  const float expected = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  CHECK(BallInertia(config) == Approx(expected).margin(1e-9f));
}

TEST_CASE("Central impulse changes linear velocity by J/m only",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
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
  const BallModel config = TestBallModel();
  BallState ball;
  ball.position = Vector3(0.0f, 0.0f, 0.0f);

  // Impulse +x applied at the bottom surface (r = -R z) gives torque r x J = -y.
  const auto next = ApplyImpulseAtPoint(
      ball, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -0.11f), config);

  CHECK(next.velocity.coords[0] == Approx(2.0f).margin(1e-5f));
  CHECK(next.angular_velocity.coords[1] < 0.0f);
  CHECK(next.angular_velocity.coords[0] == Approx(0.0f).margin(1e-6f));
  CHECK(next.angular_velocity.coords[2] == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("The lever arm comes from the applied point, not from the radius",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  BallState ball;

  // Same impulse, half the lever arm: half the spin. This is what lets a pass
  // or shot strike the ball anywhere inside its shell.
  const auto surface = ApplyImpulseAtPoint(
      ball, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -config.radius), config);
  const auto buried = ApplyImpulseAtPoint(
      ball, Vector3(1.0f, 0.0f, 0.0f),
      Vector3(0.0f, 0.0f, -0.5f * config.radius), config);

  CHECK(buried.velocity.coords[0] == Approx(surface.velocity.coords[0]));
  CHECK(buried.angular_velocity.GetLength() ==
        Approx(0.5f * surface.angular_velocity.GetLength()).margin(1e-6f));
}

TEST_CASE("Frictionless contact reflects the normal velocity by restitution",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {0.0f, 0.0f, -2.0f});
  const Vector3 normal{0.0f, 0.0f, 1.0f};
  const auto resolved = Resolve(ball, SurfaceContact(ball, normal, config.radius),
                                ContactMaterial{0.5f, 0.0f}, config);

  CHECK(resolved.impulse.normal_impulse > 0.0f);
  CHECK(resolved.state.velocity.coords[2] == Approx(1.0f).margin(1e-4f));
  CHECK(resolved.state.angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
  CHECK(TangentialImpulse(ball, resolved.state, normal,
                          resolved.impulse.normal_impulse, config)
            .GetLength() == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Separating contact produces no new normal impulse",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {0.0f, 0.0f, 2.0f});
  const auto resolved =
      Resolve(ball, SurfaceContact(ball, {0.0f, 0.0f, 1.0f}, config.radius),
              ContactMaterial{0.5f, 0.0f}, config);

  CHECK(resolved.impulse.normal_impulse == Approx(0.0f));
  CHECK(resolved.impulse.impulse.GetLength() == Approx(0.0f).margin(1e-9f));
  CHECK(resolved.state.velocity.coords[2] == Approx(2.0f));
  CHECK(resolved.state.velocity.coords[0] == Approx(0.0f));
}

TEST_CASE("Tangential impulse is Coulomb-capped by friction",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {3.0f, 0.0f, -2.0f});
  const ContactMaterial material{0.5f, 0.2f};
  const Vector3 normal{0.0f, 0.0f, 1.0f};
  const auto resolved =
      Resolve(ball, SurfaceContact(ball, normal, config.radius), material, config);

  REQUIRE(resolved.impulse.normal_impulse > 0.0f);
  CHECK(TangentialImpulse(ball, resolved.state, normal,
                          resolved.impulse.normal_impulse, config)
            .GetLength() <= material.friction * resolved.impulse.normal_impulse + 1e-5f);
}

TEST_CASE("Friction slows tangential velocity and produces spin",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {3.0f, 0.0f, -2.0f});
  const Vector3 normal{0.0f, 0.0f, 1.0f};
  const auto resolved = Resolve(ball, SurfaceContact(ball, normal, config.radius),
                                ContactMaterial{0.5f, 0.5f}, config);

  CHECK(TangentialImpulse(ball, resolved.state, normal,
                          resolved.impulse.normal_impulse, config)
            .GetLength() > 0.0f);
  CHECK(resolved.state.velocity.coords[0] < 3.0f);
  CHECK(resolved.state.angular_velocity.GetLength() > 0.0f);
}

TEST_CASE("Contact resolution takes its lever arm from the contact point",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {0.0f, 0.0f, -2.0f});
  const ContactMaterial frictionless{0.5f, 0.0f};
  const Vector3 normal{0.0f, 0.0f, 1.0f};

  // Touching along the normal: the normal impulse passes through the centre.
  const auto centered = Resolve(ball, SurfaceContact(ball, normal, config.radius),
                                frictionless, config);
  CHECK(centered.state.angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));

  // Recording the same hit half a radius off the axis turns that straight normal
  // impulse into torque r x (n * jn), i.e. spin about -y.
  BallContact offset = SurfaceContact(ball, normal, config.radius);
  offset.point = ball.position + Vector3(0.05f, 0.0f, -config.radius);
  const auto torqued = Resolve(ball, offset, frictionless, config);
  const float jn = torqued.impulse.normal_impulse;
  CHECK(torqued.state.angular_velocity.coords[1] ==
        Approx(-0.05f * jn / BallInertia(config)).margin(1e-5f));
}

TEST_CASE("Moving collider velocity enters the relative contact velocity",
          "[unit][ball][response]") {
  const BallModel config = TestBallModel();
  // Ball already moving away (+z) from a static surface: no impulse.
  const BallState ball = BallAt({0.0f, 0.0f, 0.11f}, {0.0f, 0.0f, 1.0f});
  const Vector3 normal{0.0f, 0.0f, 1.0f};
  const BallContact contact = SurfaceContact(ball, normal, config.radius);

  CHECK(Resolve(ball, contact, ContactMaterial{0.5f, 0.0f}, config)
            .impulse.normal_impulse == Approx(0.0f));

  // The same ball meets a collider moving up faster than it: approaching again.
  BallContact moving = contact;
  moving.surface_velocity = Vector3(0.0f, 0.0f, 3.0f);
  CHECK(Resolve(ball, moving, ContactMaterial{0.5f, 0.0f}, config)
            .impulse.normal_impulse > 0.0f);
}
