#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include "football/ball/ball.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"

using football::math::Vector3;
using football::ball::Ball;
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

BallModel BallWithoutMagnus() {
  return BallModel{.mass = 0.43f, .radius = 0.11f, .magnus_coefficient = 0.0f};
}

Pitch PitchWithoutResistance() {
  return Pitch(football_pitch_model{.length = 105.0f, .width = 68.0f,
                                    .quadratic_resistance = 0.0f,
                                    .ground_deceleration = 0.0f, .air_drag = 0.0f});
}

}

TEST_CASE("Free fall follows gravity", "[unit][ball][simulation]") {
  Ball ball(BallWithoutMagnus());
  const auto pitch = PitchWithoutResistance();
  ball.set_state(BallAt({0, 0, 10}, {0, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.velocity.coords[2] == Approx(-kGravity * kDt).margin(1e-4f));
  CHECK(state.position.coords[2] ==
        Approx(10.0f - 0.5f * kGravity * kDt * kDt).margin(1e-4f));
}

TEST_CASE("Horizontal flight without drag keeps horizontal velocity",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutMagnus());
  const auto pitch = PitchWithoutResistance();
  ball.set_state(BallAt({0, 0, 10}, {5, 0, 0}));
  ball.Step(pitch);

  CHECK(ball.get_state().velocity.coords[0] == Approx(5.0f).margin(1e-6f));
  CHECK(ball.get_state().position.coords[0] == Approx(5.0f * kDt).margin(1e-6f));
}

TEST_CASE("Air drag removes kinetic energy", "[unit][ball][simulation]") {
  Ball ball(BallWithoutMagnus());
  const Pitch pitch(football_pitch_model{.length = 105.0f, .width = 68.0f,
                                        .quadratic_resistance = 0.0f,
                                        .ground_deceleration = 0.0f, .air_drag = 0.02f});
  ball.set_state(BallAt({0, 0, 10}, {10, 0, 0}));
  ball.Step(pitch);

  CHECK(ball.get_state().velocity.GetLength() < 10.0f);
  CHECK(ball.get_state().velocity.coords[0] > 0.0f);
}

TEST_CASE("Spin bends a moving ball sideways (Magnus)", "[unit][ball][simulation]") {
  Ball ball(BallModel{.mass = 0.43f, .radius = 0.11f, .magnus_coefficient = 0.05f});
  const auto pitch = PitchWithoutResistance();
  auto initial = BallAt({0, 0, 10}, {5, 0, 0});
  initial.angular_velocity = Vector3(0, 0, 10);
  ball.set_state(initial);
  ball.Step(pitch);

  CHECK(ball.get_state().velocity.coords[1] > 0.0f);
  CHECK(ball.get_state().velocity.coords[1] == Approx(0.05f * 50.0f * kDt).margin(1e-4f));
}

TEST_CASE("Grass rolling decelerates horizontal speed", "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
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
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  const float restitution = pitch.colliders().front().material.restitution;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, 0.15f}, {0, 0, -5}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
  CHECK(state.velocity.coords[2] ==
        Approx(restitution * (5.0f + kGravity * kDt)).margin(1e-4f));
  const float resting_z = state.position.coords[2];
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[2] > resting_z);
}

TEST_CASE("Initial ground penetration is repaired without an impulse",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutMagnus());
  const auto pitch = PitchWithoutResistance();
  auto initial = BallAt({0, 0, 0.05f}, {1, 0, 0});
  initial.angular_velocity = Vector3(0, 0, 2);
  ball.set_state(initial);
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(ball.model().radius).margin(1e-4f));
  CHECK(state.velocity == initial.velocity);
  CHECK(state.angular_velocity == initial.angular_velocity);
  CHECK(state.orientation == football::math::IntegrateOrientation(
                                 initial.orientation, initial.angular_velocity, kDt));
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
  const BallModel config{.mass = 0.43f, .radius = 0.11f, .magnus_coefficient = 0.02f};
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
  const BallModel config{.mass = 0.43f, .radius = 0.11f, .magnus_coefficient = 0.05f};
  const auto pitch = PitchWithoutResistance();
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
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
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
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, 10}));
  for (int i = 0; i < 100; ++i) ball.Step(pitch);
  CHECK(ball.get_state().position.coords[2] ==
        Approx(config.radius + 10.0f - 0.5f * kGravity).margin(0.001f));
  CHECK(ball.get_state().velocity.coords[2] == Approx(10.0f - kGravity).margin(0.001f));
}

TEST_CASE("Drag-free oblique flight matches analytic trajectory", "[unit][ball][simulation]") {
  const auto pitch = PitchWithoutResistance();
  Ball ball(BallWithoutMagnus());
  ball.set_state(BallAt({0, 0, 2}, {12, 3, 8}));
  for (int i = 0; i < 50; ++i) ball.Step(pitch);
  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(6.0f).margin(0.001f));
  CHECK(state.position.coords[1] == Approx(1.5f).margin(0.001f));
  CHECK(state.position.coords[2] == Approx(2 + 4 - 0.5f * kGravity * 0.25f).margin(0.001f));
}

TEST_CASE("Successive ground bounces keep the pitch restitution and never sink",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  const float restitution = pitch.colliders().front().material.restitution;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, -2}));
  float previous = -2.0f;
  int bounces = 0;
  for (int i = 0; i < 300; ++i) {
    ball.Step(pitch);
    const auto state = ball.get_state();
    const float vz = state.velocity.coords[2];
    CHECK(state.position.coords[2] >= config.radius - 5e-4f);
    CHECK(state.position.coords[2] <= config.radius + 0.6f);
    if (previous <= 0.0f && vz > 0.0f) {
      ++bounces;
      CHECK(vz >= restitution * -previous - 1e-4f);
      CHECK(vz <= restitution * (-previous + kGravity * kDt) + 1e-4f);
    }
    previous = vz;
  }
  CHECK(bounces >= 3);
}

TEST_CASE("A collision tick stops at the first post, not the second",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  const auto& high = pitch.colliders()[4];
  const auto& post = std::get<Capsule>(high.shape);
  const float touching_y = pitch.goal_half_width() - post.radius - config.radius;
  Ball ball(config);
  ball.set_state(BallAt({pitch.half_length(), 0, config.radius}, {0, 1000, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[1] == Approx(touching_y).margin(1e-5f));
  CHECK(state.velocity.coords[1] == Approx(-high.material.restitution * 1000));
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[1] < 0.0f);
  CHECK(ball.get_state().velocity.coords[1] > 0.0f);
}

TEST_CASE("A tiny ground launch is absorbed as rolling",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, 0.045f}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
  CHECK(state.velocity.coords[2] == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Ground resistance is predicted for the full tick before a post impact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const Pitch pitch(football_pitch_model{.length = 105, .width = 68,
                                        .quadratic_resistance = 0,
                                        .ground_deceleration = 1, .air_drag = 0.025f});
  const auto& collider = pitch.colliders()[3];
  const auto& post = std::get<Capsule>(collider.shape);
  const float touching_x = pitch.half_length() - post.radius - config.radius;
  Ball ball(config);
  ball.set_state(BallAt({touching_x - 0.09f, -pitch.goal_half_width(), config.radius},
                        {30, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(touching_x).margin(1e-5f));
  CHECK(state.velocity.coords[0] ==
        Approx(-collider.material.restitution * (30.0f - 1.0f * kDt)).margin(1e-4f));
}

TEST_CASE("Step commits every state component after a collision",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutMagnus());
  const auto pitch = PitchWithoutResistance();
  auto initial = BallAt({pitch.half_length() - 0.5f, -pitch.goal_half_width(), 1}, {40, 0, 0});
  initial.angular_velocity = Vector3(0, 1, 2);
  initial.orientation.SetAngleAxis(0.3f, {1, 0, 0});
  ball.set_state(initial);
  ball.Step(pitch);

  const auto now = ball.get_state();
  CHECK(now.position.coords[0] < initial.position.coords[0] + 40 * kDt);
  CHECK(now.velocity.coords[0] < 0.0f);
  CHECK(now.angular_velocity == initial.angular_velocity);
  CHECK(now.orientation != initial.orientation);
}

TEST_CASE("Step advances exactly one fixed tick", "[unit][ball][simulation]") {
  Ball ball(BallWithoutMagnus());
  const auto pitch = PitchWithoutResistance();
  ball.set_state(BallAt({1, 2, 3}, {4, 5, 6}));
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[0] == Approx(1.0f + 4 * kDt));
  CHECK(ball.get_state().velocity.coords[2] == Approx(6.0f - kGravity * kDt));
  ball.Step(pitch);
  CHECK(ball.get_state().position.coords[0] == Approx(1.0f + 4 * 2 * kDt));
  CHECK(ball.get_state().velocity.coords[2] == Approx(6.0f - kGravity * 2 * kDt));
}

TEST_CASE("Passive response applies an impulse to the predicted state",
          "[unit][ball][simulation]") {
  const BallModel config{.mass = 0.43f, .radius = 0.11f, .magnus_coefficient = 0.05f};
  const Pitch pitch(football_pitch_model{.length = 105, .width = 68,
                                        .quadratic_resistance = 0,
                                        .ground_deceleration = 0, .air_drag = 0.02f});
  auto initial = BallAt({0, 0, 0.15f}, {3, 1, -5});
  initial.angular_velocity = Vector3(0, 2, 3);
  initial.orientation.SetAngleAxis(0.3f, {1, 0, 0});
  const auto& ground = pitch.colliders().front();

  const auto& v = initial.velocity;
  const auto& omega = initial.angular_velocity;
  const auto accel = Vector3(0, 0, -kGravity) - v * (pitch.air_drag() * v.GetLength()) +
                     omega.GetCrossProduct(v) * config.magnus_coefficient;
  BallState predicted = initial;
  predicted.position = initial.position + (v + accel * (0.5f * kDt)) * kDt;
  predicted.velocity = v + accel * kDt;
  const football::math::Quaternion omega_q(
      omega.coords[0], omega.coords[1], omega.coords[2], 0);
  predicted.orientation =
      (initial.orientation + omega_q * initial.orientation * (0.5f * kDt)).GetNormalized();

  const float fraction = (config.radius - initial.position.coords[2]) /
                         (predicted.position.coords[2] - initial.position.coords[2]);
  REQUIRE(fraction > 0.0f);
  REQUIRE(fraction < 1.0f);

  Ball expected(config);
  BallState at_contact = predicted;
  at_contact.position = initial.position + (predicted.position - initial.position) * fraction;
  expected.set_state(at_contact);
  const Vector3 contact_normal(0, 0, 1);
  const Vector3 contact_point = at_contact.position - contact_normal * config.radius;
  const auto solved = expected.SolveContactImpulse(
      contact_normal, contact_point, Vector3(0, 0, 0), ground.material);
  REQUIRE(solved.has_value());
  expected.ApplyImpulseAtPoint(*solved, contact_point);

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
  CHECK(state.orientation == predicted.orientation);
}

TEST_CASE("A fast path is swept even when both endpoints are outside a post",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  const auto& collider = pitch.colliders()[3];
  const auto& post = std::get<Capsule>(collider.shape);
  const float touching_x = pitch.half_length() - post.radius - config.radius;
  Ball ball(config);
  ball.set_state(BallAt({pitch.half_length() - 1, -pitch.goal_half_width(), config.radius},
                        {200, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(touching_x).margin(1e-5f));
  CHECK(state.velocity.coords[0] == Approx(-collider.material.restitution * 200).margin(1e-4f));
}

TEST_CASE("A ball at rest on the grass is integrated to the same state",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  Ball ball(config);
  ball.PlaceOnGround(3, 4);
  const auto before = ball.get_state();
  for (int i = 0; i < 5; ++i) ball.Step(pitch);
  const auto after = ball.get_state();
  CHECK(after.position == before.position);
  CHECK(after.velocity == before.velocity);
  CHECK(after.angular_velocity == before.angular_velocity);
  CHECK(after.orientation == before.orientation);

  auto spinning = before;
  spinning.angular_velocity = Vector3(0, 0, 4);
  ball.set_state(spinning);
  ball.Step(pitch);
  CHECK(ball.get_state().orientation != spinning.orientation);

  ball.set_state(BallAt({0, 0, 5}, {0, 0, 0}));
  ball.Step(pitch);
  CHECK(ball.get_state().velocity.coords[2] == Approx(-kGravity * kDt).margin(1e-4f));
}

TEST_CASE("A drop bounces off the predicted tick-end speed",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  const float restitution = pitch.colliders().front().material.restitution;

  const float drop = 3e-4f;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius + drop}, {0, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
  CHECK(state.velocity.coords[2] == Approx(restitution * kGravity * kDt).margin(1e-4f));
}

TEST_CASE("A post impact keeps the remaining tick time unspent",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  const auto& post_collider = pitch.colliders()[3];
  const auto& post = std::get<Capsule>(post_collider.shape);
  const float touching_x = pitch.half_length() - post.radius - config.radius;

  Ball ball(config);
  ball.set_state(BallAt({touching_x - 100 * 0.004f, -pitch.goal_half_width(),
                         config.radius},
                        {100, 0, 0}));
  ball.Step(pitch);

  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(touching_x).margin(1e-5f));
  CHECK(state.velocity.coords[0] == Approx(-post_collider.material.restitution * 100));
  CHECK(state.position.coords[0] > touching_x - 0.01f);
}

TEST_CASE("A rolling ball is not blocked by its own ground contact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const Pitch pitch;
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {4, 0, 0}));
  float previous_x = 0.0f;
  for (int i = 0; i < 20; ++i) {
    ball.Step(pitch);
    const auto state = ball.get_state();
    CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
    CHECK(state.position.coords[0] > previous_x);
    CHECK(state.velocity.coords[2] == Approx(0.0f).margin(1e-6f));
    previous_x = state.position.coords[0];
  }
  CHECK(ball.get_state().velocity.coords[0] < 4.0f);
}

TEST_CASE("A tiny lift off the grass is absorbed as resting",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const auto pitch = PitchWithoutResistance();
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, 0.01f}));

  for (int i = 0; i < 4; ++i) {
    ball.Step(pitch);
    const auto state = ball.get_state();
    INFO("tick " << i << " z=" << state.position.coords[2]
                 << " vz=" << state.velocity.coords[2]);
    CHECK(state.position.coords[2] == Approx(config.radius).margin(1e-5f));
    CHECK(state.velocity.coords[2] == Approx(0.0f).margin(1e-6f));
  }
}

TEST_CASE("Grass dissipates grounded spin without reversing it",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  const Pitch pitch;
  for (const Vector3 spin : {Vector3(0, -30, 0), Vector3(0, 0, 30), Vector3(12, -24, 6)}) {
    Ball ball(config);
    auto initial = BallAt({0, 0, config.radius}, {0, 0, 0});
    initial.angular_velocity = spin;
    ball.set_state(initial);
    float previous_speed = spin.GetLength();
    for (int tick = 0; tick < 1000; ++tick) {
      ball.Step(pitch);
      const auto state = ball.get_state();
      CHECK(state.angular_velocity.GetLength() <= previous_speed + 1e-5f);
      CHECK(state.angular_velocity.GetDotProduct(spin) >= 0.0f);
      CHECK(state.position == initial.position);
      previous_speed = state.angular_velocity.GetLength();
    }
    CHECK(ball.get_state().angular_velocity == Vector3(0, 0, 0));
    const auto stopped = ball.get_state();
    ball.Step(pitch);
    CHECK(ball.get_state().orientation == stopped.orientation);
  }
}

TEST_CASE("Ground spin resistance preserves spin without grass resistance",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  Ball ball(config);
  auto state = BallAt({0, 0, config.radius}, {1, 0, 0});
  state.angular_velocity = Vector3(1, -3, 5);
  ball.set_state(state);
  for (int tick = 0; tick < 100; ++tick) ball.Step(PitchWithoutResistance());
  CHECK(ball.get_state().angular_velocity == state.angular_velocity);
}

TEST_CASE("Grass spin resistance does not damp airborne spin",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutMagnus());
  auto state = BallAt({0, 0, 10}, {1, 0, 0});
  state.angular_velocity = Vector3(1, -3, 5);
  ball.set_state(state);
  ball.Step(Pitch{});
  CHECK(ball.get_state().angular_velocity == state.angular_velocity);
}

TEST_CASE("Sub-tick ground rebounds settle and stop spinning",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutMagnus();
  Ball ball(config);
  auto state = BallAt({0, 0, config.radius}, {0, 0, 0.112f});
  state.angular_velocity = Vector3(0, -15, 0);
  ball.set_state(state);
  for (int tick = 0; tick < 1000; ++tick) ball.Step(Pitch{});
  CHECK(ball.get_state().position.coords[2] == Approx(config.radius));
  CHECK(ball.get_state().velocity == Vector3(0, 0, 0));
  CHECK(ball.get_state().angular_velocity == Vector3(0, 0, 0));
}
