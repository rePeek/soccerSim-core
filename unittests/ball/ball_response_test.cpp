// Ball impulse response: analytic invariants exercised through Ball members.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/vector3.hpp"

using football::math::Vector3;
using football::ball::Ball;
using football::ball::BallContact;
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

BallContact SurfaceContact(const Ball& ball, const Vector3& normal) {
  BallContact contact;
  contact.collider = 1;
  contact.toi = 0.5f;
  contact.normal = normal;
  contact.point = ball.get_state().position - normal * ball.model().radius;
  return contact;
}

Ball::ContactImpulse Resolve(Ball& ball, const BallContact& contact,
                             const ContactMaterial& material) {
  const auto solved = ball.SolveContactImpulse(contact, material);
  ball.ApplyImpulseAtPoint(solved.impulse, contact.point);
  return solved;
}

Vector3 TangentialImpulse(const BallState& before, const Ball& after,
                          const Vector3& normal, float normal_impulse) {
  const Vector3 total = (after.get_state().velocity - before.velocity) * after.model().mass;
  return total - normal * normal_impulse;
}

}  // namespace

TEST_CASE("Impulse response uses thin-shell sphere inertia", "[unit][ball][response]") {
  Ball ball(TestBallModel());
  const float expected = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  ball.ApplyImpulseAtPoint({1, 0, 0}, {0, 0, -0.11f});
  const float inferred = -0.11f / ball.get_state().angular_velocity.coords[1];
  CHECK(inferred == Approx(expected).margin(1e-9f));
}

TEST_CASE("Central impulse changes linear velocity by J/m only",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  const auto before = ball.get_state();
  ball.ApplyImpulseAtPoint({1, 0, 0}, before.position);
  const auto next = ball.get_state();

  CHECK(next.velocity.coords[0] == Approx(2.0f).margin(1e-5f));
  CHECK(next.velocity.coords[1] == Approx(0.0f));
  CHECK(next.velocity.coords[2] == Approx(0.0f));
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

TEST_CASE("The lever arm comes from the applied point, not from the radius",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  const auto initial = ball.get_state();
  ball.ApplyImpulseAtPoint({1, 0, 0}, {0, 0, -ball.model().radius});
  const auto surface = ball.get_state();
  ball.set_state(initial);
  ball.ApplyImpulseAtPoint({1, 0, 0}, {0, 0, -0.5f * ball.model().radius});
  const auto buried = ball.get_state();

  CHECK(buried.velocity.coords[0] == Approx(surface.velocity.coords[0]));
  CHECK(buried.angular_velocity.GetLength() ==
        Approx(0.5f * surface.angular_velocity.GetLength()).margin(1e-6f));
}

TEST_CASE("Frictionless contact reflects the normal velocity by restitution",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {0, 0, -2}));
  const auto before = ball.get_state();
  const Vector3 normal{0, 0, 1};
  const auto solved = Resolve(ball, SurfaceContact(ball, normal), {0.5f, 0});

  CHECK(solved.normal_impulse > 0.0f);
  CHECK(ball.get_state().velocity.coords[2] == Approx(1.0f).margin(1e-4f));
  CHECK(ball.get_state().angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
  CHECK(TangentialImpulse(before, ball, normal, solved.normal_impulse).GetLength() ==
        Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Separating contact produces no new normal impulse",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {0, 0, 2}));
  const auto solved = Resolve(ball, SurfaceContact(ball, {0, 0, 1}), {0.5f, 0});

  CHECK(solved.normal_impulse == Approx(0.0f));
  CHECK(solved.impulse.GetLength() == Approx(0.0f).margin(1e-9f));
  CHECK(ball.get_state().velocity.coords[2] == Approx(2.0f));
  CHECK(ball.get_state().velocity.coords[0] == Approx(0.0f));
}

TEST_CASE("Tangential impulse is Coulomb-capped by friction",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {3, 0, -2}));
  const auto before = ball.get_state();
  const ContactMaterial material{0.5f, 0.2f};
  const Vector3 normal{0, 0, 1};
  const auto solved = Resolve(ball, SurfaceContact(ball, normal), material);

  REQUIRE(solved.normal_impulse > 0.0f);
  CHECK(TangentialImpulse(before, ball, normal, solved.normal_impulse).GetLength() <=
        material.friction * solved.normal_impulse + 1e-5f);
}

TEST_CASE("Friction slows tangential velocity and produces spin",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {3, 0, -2}));
  const auto before = ball.get_state();
  const Vector3 normal{0, 0, 1};
  const auto solved = Resolve(ball, SurfaceContact(ball, normal), {0.5f, 0.5f});

  CHECK(TangentialImpulse(before, ball, normal, solved.normal_impulse).GetLength() > 0.0f);
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

  Resolve(ball, SurfaceContact(ball, normal), frictionless);
  CHECK(ball.get_state().angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));

  ball.set_state(initial);
  auto offset = SurfaceContact(ball, normal);
  offset.point = initial.position + Vector3(0.05f, 0, -ball.model().radius);
  const auto solved = Resolve(ball, offset, frictionless);
  const float expected_inertia = (2.0f / 3.0f) * 0.5f * 0.11f * 0.11f;
  CHECK(ball.get_state().angular_velocity.coords[1] ==
        Approx(-0.05f * solved.normal_impulse / expected_inertia).margin(1e-5f));
}

TEST_CASE("Moving collider velocity enters the relative contact velocity",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({0, 0, 0.11f}, {0, 0, 1}));
  auto contact = SurfaceContact(ball, {0, 0, 1});

  CHECK(ball.SolveContactImpulse(contact, {0.5f, 0}).normal_impulse == Approx(0.0f));
  contact.surface_velocity = Vector3(0, 0, 3);
  CHECK(ball.SolveContactImpulse(contact, {0.5f, 0}).normal_impulse > 0.0f);
}

TEST_CASE("Solving uses current state without applying the impulse",
          "[unit][ball][response]") {
  Ball ball(TestBallModel());
  ball.set_state(BallAt({1, 2, 0.11f}, {0, 0, -2}));
  const auto contact = SurfaceContact(ball, {0, 0, 1});
  const auto before = ball.get_state();
  const auto solved = ball.SolveContactImpulse(contact, {0.5f, 0});
  CHECK(solved.normal_impulse == Approx(1.5f));
  CHECK(ball.get_state().position == before.position);
  CHECK(ball.get_state().velocity == before.velocity);
  CHECK(ball.get_state().angular_velocity == before.angular_velocity);
  CHECK_FALSE(ball.get_state().orientation != before.orientation);

  ball.ApplyImpulseAtPoint(solved.impulse, contact.point);
  CHECK(ball.SolveContactImpulse(contact, {0.5f, 0}).normal_impulse == Approx(0.0f));
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
