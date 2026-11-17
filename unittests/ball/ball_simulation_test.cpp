// Ball one-tick physics through the public Step API and the Pitch scene.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include "football/ball/ball.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"

using football::math::Vector3;
using football::ball::Ball;
using football::ball::BallContact;
using football::ball::BallModel;
using football::ball::BallState;
using football::geometry::Capsule;
using football::pitch::Pitch;
using Catch::Approx;

namespace {

constexpr float kDt = football::time::kTickSeconds;
constexpr float kGravity = 9.81f;

BallState BallAt(const Vector3& position, const Vector3& velocity) {
  BallState state;
  state.position = position;
  state.velocity = velocity;
  return state;
}

BallModel BallWithoutAero() { return BallModel{0.43f, 0.11f, 0.0f}; }

Pitch FrictionlessPitch() {
  return Pitch(football_pitch_model{105.0f, 68.0f, 0.0f, 0.0f, 0.0f});
}

}  // namespace

TEST_CASE("Free fall follows gravity", "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const auto pitch = FrictionlessPitch();
  ball.set_state(BallAt({0, 0, 10}, {0, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.velocity.coords[2] == Approx(-kGravity * kDt).margin(1e-4f));
  CHECK(state.position.coords[2] ==
        Approx(10.0f - 0.5f * kGravity * kDt * kDt).margin(1e-4f));
}

TEST_CASE("Horizontal flight without drag keeps horizontal velocity",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const auto pitch = FrictionlessPitch();
  ball.set_state(BallAt({0, 0, 10}, {5, 0, 0}));
  ball.Step(pitch);

  CHECK(ball.get_state().velocity.coords[0] == Approx(5.0f).margin(1e-6f));
  CHECK(ball.get_state().position.coords[0] == Approx(5.0f * kDt).margin(1e-6f));
}

TEST_CASE("Air drag removes kinetic energy", "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const Pitch pitch(football_pitch_model{105.0f, 68.0f, 0.0f, 0.0f, 0.02f});
  ball.set_state(BallAt({0, 0, 10}, {10, 0, 0}));
  ball.Step(pitch);

  CHECK(ball.get_state().velocity.GetLength() < 10.0f);
  CHECK(ball.get_state().velocity.coords[0] > 0.0f);
}

TEST_CASE("Spin bends a moving ball sideways (Magnus)", "[unit][ball][simulation]") {
  Ball ball(BallModel{0.43f, 0.11f, 0.05f});
  const auto pitch = FrictionlessPitch();
  auto initial = BallAt({0, 0, 10}, {5, 0, 0});
  initial.angular_velocity = Vector3(0, 0, 10);
  ball.set_state(initial);
  ball.Step(pitch);

  CHECK(ball.get_state().velocity.coords[1] > 0.0f);
  CHECK(ball.get_state().velocity.coords[1] == Approx(0.05f * 50.0f * kDt).margin(1e-4f));
}

TEST_CASE("Grass rolling decelerates horizontal speed", "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const Pitch pitch;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {5, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(config.radius));
  CHECK(state.velocity.coords[0] < 5.0f);
  CHECK(state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Landing uses the pitch ground material and stops at contact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const float restitution = pitch.colliders().front().material.restitution;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, 0.15f}, {0, 0, -5}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
  CHECK(state.velocity.coords[2] == Approx(restitution * (5.0f + kGravity * kDt)));
  const float resting_z = state.position.coords[2];
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[2] > resting_z);
}

TEST_CASE("Initial ground penetration is repaired without an impulse",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const auto pitch = FrictionlessPitch();
  ball.set_state(BallAt({0, 0, 0.05f}, {1, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(ball.model().radius).margin(1e-4f));
  CHECK(state.velocity.coords[1] == Approx(0.0f));
  CHECK(state.angular_velocity.GetLength() == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("set_state restores the exact state", "[unit][ball][simulation]") {
  Ball ball;
  auto state = BallAt({1, 2, 3}, {4, 5, 6});
  state.angular_velocity = Vector3(7, 8, 9);
  ball.set_state(state);

  CHECK(ball.get_state().position.coords[0] == Approx(1.0f));
  CHECK(ball.get_state().position.coords[2] == Approx(3.0f));
  CHECK(ball.get_state().velocity.coords[1] == Approx(5.0f));
  CHECK(ball.get_state().angular_velocity.coords[2] == Approx(9.0f));
}

TEST_CASE("ApplyImpulseAtPoint uses the ball's model and current state",
          "[unit][ball][simulation]") {
  const BallModel config{0.43f, 0.11f, 0.02f};
  Ball ball(config);
  const auto initial = BallAt({0, 0, 10}, {0, 0, 0});
  ball.set_state(initial);
  const Vector3 impulse{6, 0, 0};
  const Vector3 foot{0, 0, 10.0f - config.radius};
  ball.ApplyImpulseAtPoint(impulse, foot);

  const auto committed = ball.get_state();
  CHECK(committed.velocity.coords[0] == Approx(impulse.coords[0] / config.mass));
  const float arm = foot.coords[2] - initial.position.coords[2];
  const float expected_inertia =
      (2.0f / 3.0f) * config.mass * config.radius * config.radius;
  CHECK(committed.angular_velocity.coords[1] ==
        Approx(arm * impulse.coords[0] / expected_inertia));
  CHECK(committed.angular_velocity.coords[1] < 0.0f);
  CHECK(committed.position.coords[2] == Approx(initial.position.coords[2]));
}

TEST_CASE("A struck ball carries its spin into the next tick (Magnus)",
          "[unit][ball][simulation]") {
  const BallModel config{0.43f, 0.11f, 0.05f};
  const auto pitch = FrictionlessPitch();
  Ball ball(config);
  ball.set_state(BallAt({0, 0, 10}, {5, 0, 0}));
  ball.ApplyImpulseAtPoint({0, 0.6f, 0}, {config.radius, 0, 10});
  REQUIRE(ball.get_state().angular_velocity.coords[2] > 0.0f);
  const float before_y = ball.get_state().velocity.coords[1];
  ball.Step(pitch);
  CHECK(ball.get_state().velocity.coords[1] > before_y);
}

TEST_CASE("PlaceOnGround sets ground position and clears motion",
          "[unit][ball][simulation]") {
  Ball ball;
  ball.set_state(BallAt({1, 2, 10}, {3, 4, 5}));
  ball.PlaceOnGround(7, 8);
  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(7.0f));
  CHECK(state.position.coords[1] == Approx(8.0f));
  CHECK(state.position.coords[2] == Approx(ball.model().radius));
  CHECK(state.velocity.GetLength() == Approx(0.0f));
  CHECK(state.angular_velocity.GetLength() == Approx(0.0f));
}

TEST_CASE("Ball flies freely through former goal net regions",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  Vector3 position(pitch.half_length() - 0.05f, 0, 1);
  Vector3 velocity(40, 0, 0);
  SECTION("goal line") {}
  SECTION("rear net") { position.coords[0] = pitch.half_length() + pitch.goal_depth(); }
  SECTION("side net") {
    position = Vector3(pitch.half_length() + 1, pitch.goal_half_width(), 1);
    velocity = Vector3(0, 5, 0);
  }
  SECTION("top net") {
    position = Vector3(pitch.half_length() + 1, 0, pitch.goal_height());
    velocity = Vector3(0, 0, 5);
  }
  for (const float side : {-1.0f, 1.0f}) {
    auto initial = BallAt(position, velocity);
    initial.position.coords[0] *= side;
    initial.velocity.coords[0] *= side;
    Ball ball(config);
    ball.set_state(initial);
    ball.Step(pitch);
    const auto state = ball.get_state();
    for (int axis = 0; axis < 3; ++axis) {
      const float gravity = axis == 2 ? -kGravity : 0.0f;
      CHECK(state.velocity.coords[axis] ==
            Approx(initial.velocity.coords[axis] + gravity * kDt).margin(1e-5f));
      CHECK(state.position.coords[axis] ==
            Approx(initial.position.coords[axis] + initial.velocity.coords[axis] * kDt +
                   0.5f * gravity * kDt * kDt).margin(1e-5f));
    }
  }
}

TEST_CASE("Ground kick launches on subsequent ticks", "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, 10}));
  for (int i = 0; i < 100; ++i) ball.Step(pitch);
  CHECK(ball.get_state().position.coords[2] ==
        Approx(config.radius + 10.0f - 0.5f * kGravity).margin(0.001f));
  CHECK(ball.get_state().velocity.coords[2] == Approx(10.0f - kGravity).margin(0.001f));
}

TEST_CASE("Drag-free oblique flight matches analytic trajectory", "[unit][ball][simulation]") {
  const auto pitch = FrictionlessPitch();
  Ball ball(BallWithoutAero());
  ball.set_state(BallAt({0, 0, 2}, {12, 3, 8}));
  for (int i = 0; i < 50; ++i) ball.Step(pitch);
  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(6.0f).margin(0.001f));
  CHECK(state.position.coords[1] == Approx(1.5f).margin(0.001f));
  CHECK(state.position.coords[2] == Approx(2 + 4 - 0.5f * kGravity * 0.25f).margin(0.001f));
}

TEST_CASE("Successive ground bounces keep the pitch restitution and never sink",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const float restitution = pitch.colliders().front().material.restitution;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, -2}));
  float previous = -2.0f;
  int bounces = 0;
  for (int i = 0; i < 300; ++i) {
    ball.Step(pitch);
    const auto state = ball.get_state();
    const float vz = state.velocity.coords[2];
    CHECK(state.position.coords[2] >= config.radius - 1e-5f);
    CHECK(state.position.coords[2] <= config.radius + 0.6f);
    if (previous <= 0.0f && vz > 0.0f) {
      ++bounces;
      // The bounce consumes the predicted velocity, not the tick-start one.
      CHECK(vz == Approx(restitution * (-previous + kGravity * kDt)).margin(1e-4f));
      CHECK(vz <= -previous + kGravity * kDt);
    }
    previous = vz;
  }
  CHECK(bounces >= 3);
}

TEST_CASE("A collision tick stops at the first post, not the second",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  // Scene order: ground, left posts (low, high), right posts (low, high), bars.
  const auto& high = pitch.colliders()[4];
  const auto& post = std::get<Capsule>(high.start);
  const float touching_y = pitch.goal_half_width() - post.radius - config.radius;
  Ball ball(config);
  ball.set_state(BallAt({pitch.half_length(), 0, config.radius}, {0, 1000, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[1] == Approx(touching_y).margin(1e-5f));
  CHECK(state.velocity.coords[1] == Approx(-high.material.restitution * 1000));
  // The far post is only reachable on the following tick.
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[1] < 0.0f);
  CHECK(ball.get_state().velocity.coords[1] > 0.0f);
}

TEST_CASE("Small ground launch resolves the contact without an apex split",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const float restitution = pitch.colliders().front().material.restitution;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, 0.045f}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-6f));
  CHECK(state.velocity.coords[2] == Approx(restitution * (kGravity * kDt - 0.045f)));
}

TEST_CASE("Ground resistance is predicted for the full tick before a post impact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const Pitch pitch(football_pitch_model{105, 68, 0, 1, 0.025f});
  const auto& collider = pitch.colliders()[3];
  const auto& post = std::get<Capsule>(collider.start);
  const float touching_x = pitch.half_length() - post.radius - config.radius;
  Ball ball(config);
  ball.set_state(BallAt({touching_x - 0.09f, -pitch.goal_half_width(), config.radius},
                        {30, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(touching_x).margin(1e-5f));
  CHECK(state.velocity.coords[0] == Approx(-collider.material.restitution * 29.99f).margin(1e-4f));
}

TEST_CASE("Step commits every state component after a collision",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const auto pitch = FrictionlessPitch();
  auto initial = BallAt({pitch.half_length() - 0.5f, -pitch.goal_half_width(), 1}, {40, 0, 0});
  initial.angular_velocity = Vector3(0, 1, 2);
  initial.orientation.SetAngleAxis(0.3f, {1, 0, 0});
  ball.set_state(initial);
  ball.Step(pitch);

  const auto now = ball.get_state();
  CHECK(now.position.coords[0] < initial.position.coords[0] + 40 * kDt);
  CHECK(now.velocity.coords[0] < 0.0f);
  CHECK(now.angular_velocity == initial.angular_velocity);
  // Orientation integrates the spin over the whole tick, contact or not.
  const auto& omega = initial.angular_velocity;
  const football::math::Quaternion omega_q(
      omega.coords[0], omega.coords[1], omega.coords[2], 0);
  const auto expected_orientation =
      (initial.orientation + omega_q * initial.orientation * (0.5f * kDt)).GetNormalized();
  CHECK_FALSE(now.orientation != expected_orientation);
}

TEST_CASE("Step advances exactly one fixed tick", "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const auto pitch = FrictionlessPitch();
  ball.set_state(BallAt({1, 2, 3}, {4, 5, 6}));
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[0] == Approx(1.0f + 4 * kDt));
  CHECK(ball.get_state().velocity.coords[2] == Approx(6.0f - kGravity * kDt));
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[0] == Approx(1.0f + 4 * 2 * kDt));
  CHECK(ball.get_state().velocity.coords[2] == Approx(6.0f - kGravity * 2 * kDt));
}

TEST_CASE("Ground and post overlap repairs stay internal without hiding an impact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const auto& high = pitch.colliders()[4];
  const auto& post = std::get<Capsule>(high.start);
  const float radius_sum = config.radius + post.radius;
  Ball ball(config);

  SECTION("two separating overlaps are repaired and motion is committed") {
    ball.set_state(BallAt({pitch.half_length(), -pitch.goal_half_width() + 0.05f, 0.05f},
                          {0, 1, 0}));
    ball.Step(pitch);
    const auto state = ball.get_state();
    CHECK(state.position.coords[1] ==
          Approx(-pitch.goal_half_width() + radius_sum + kDt).margin(1e-5f));
    CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
    CHECK(state.velocity.coords[1] == Approx(1.0f).margin(1e-6f));
  }
  SECTION("an impact after both repairs still resolves on the predicted velocity") {
    const float touching_y = pitch.goal_half_width() - radius_sum;
    const float start_y = -pitch.goal_half_width() + 0.05f;
    const float travel = 2000.0f * kDt;
    const float fraction = (touching_y - start_y) / travel;
    REQUIRE(fraction > 0.0f);
    REQUIRE(fraction < 1.0f);
    ball.set_state(BallAt({pitch.half_length(), start_y, 0.05f}, {0, 2000, 0}));
    ball.Step(pitch);
    const auto state = ball.get_state();
    CHECK(state.position.coords[1] == Approx(touching_y).margin(1e-5f));
    CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
    CHECK(state.velocity.coords[1] == Approx(-high.material.restitution * 2000));
  }
}

TEST_CASE("Passive response applies an impulse to the predicted full-tick state",
          "[unit][ball][simulation]") {
  const BallModel config{0.43f, 0.11f, 0.05f};
  const Pitch pitch(football_pitch_model{105, 68, 0, 0, 0.02f});
  auto initial = BallAt({0, 0, 0.15f}, {3, 1, -5});
  initial.angular_velocity = Vector3(0, 2, 3);
  initial.orientation.SetAngleAxis(0.3f, {1, 0, 0});
  const auto& ground = pitch.colliders().front();

  // Analytic full-tick prediction, independent of the collision branch.
  const auto& v = initial.velocity;
  const auto& omega = initial.angular_velocity;
  const auto accel = Vector3(0, 0, -kGravity) - v * (pitch.air_drag() * v.GetLength()) +
                     omega.GetCrossProduct(v) * config.magnus_coefficient;
  BallState free = initial;
  free.position = initial.position + (v + accel * (0.5f * kDt)) * kDt;
  free.velocity = v + accel * kDt;
  const football::math::Quaternion omega_q(
      omega.coords[0], omega.coords[1], omega.coords[2], 0);
  free.orientation =
      (initial.orientation + omega_q * initial.orientation * (0.5f * kDt)).GetNormalized();

  Ball expected(config);
  BallState touching = free;
  touching.position = initial.position +
      (free.position - initial.position) *
          ((config.radius - initial.position.coords[2]) /
           (free.position.coords[2] - initial.position.coords[2]));
  expected.set_state(touching);
  BallContact contact;
  contact.normal = Vector3(0, 0, 1);
  contact.point = touching.position - contact.normal * config.radius;
  const auto solved = expected.SolveContactImpulse(contact, ground.material);
  expected.ApplyImpulseAtPoint(solved.impulse, contact.point);

  Ball ball(config);
  ball.set_state(initial);
  ball.Step(pitch);

  const auto expected_state = expected.get_state();
  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
  for (int axis = 0; axis < 3; ++axis) {
    CHECK(state.position.coords[axis] ==
          Approx(expected_state.position.coords[axis]).margin(1e-5f));
    CHECK(state.velocity.coords[axis] ==
          Approx(expected_state.velocity.coords[axis]).margin(1e-5f));
    CHECK(state.angular_velocity.coords[axis] ==
          Approx(expected_state.angular_velocity.coords[axis]).margin(1e-5f));
  }
  CHECK_FALSE(state.orientation != free.orientation);
}

TEST_CASE("A fast path is swept even when both endpoints are outside a post",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const auto& collider = pitch.colliders()[3];
  const auto& post = std::get<Capsule>(collider.start);
  const float touching_x = pitch.half_length() - post.radius - config.radius;
  Ball ball(config);
  ball.set_state(BallAt({pitch.half_length() - 1, -pitch.goal_half_width(), config.radius},
                        {200, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(touching_x).margin(1e-5f));
  CHECK(state.velocity.coords[0] == Approx(-collider.material.restitution * 200).margin(1e-4f));
}
