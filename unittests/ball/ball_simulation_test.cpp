// Ball one-tick physics: free motion, drag, spin, grass rolling, landing and
// the Ball::Step lifecycle.
//
// Long-lived parameters are supplied at construction through BallModel (the
// ball) and Pitch (gravity, ground, static colliders); a tick advances the ball
// inside that fixed scene only.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <vector>

#include "football/ball/ball.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/pitch/pitch.hpp"

using football::math::Vector3;
using football::ball::Ball;
using football::ball::BallModel;
using football::ball::BallState;
using football::geometry::ColliderMotion;
using football::geometry::ContactMaterial;
using football::geometry::HalfSpace;
using football::pitch::Pitch;
using Catch::Approx;

namespace {

constexpr float kDt = football::time::kTickSeconds;

BallState BallAt(const Vector3& position, const Vector3& velocity) {
  BallState state;
  state.position = position;
  state.velocity = velocity;
  return state;
}

// A ball with no air drag and no Magnus coupling.
BallModel BallWithoutAero() { return BallModel{0.43f, 0.11f, 0.0f}; }

// A pitch whose ground does not slow the ball, so gravity/spin tests are pure.
Pitch FrictionlessPitch() {
  return Pitch(football_pitch_model{105.0f, 68.0f, 0.0f, 0.0f, 0.0f});
}

ColliderMotion GroundPlane(float restitution) {
  const HalfSpace plane{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
  return ColliderMotion{100, plane, plane, ContactMaterial{restitution, 0.0f}};
}

}  // namespace

TEST_CASE("Free fall follows gravity", "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});

  Ball ball(config);
  ball.set_state(initial);
  const auto result = ball.AdvanceBallTick({}, kDt, pitch);

  CHECK(result.state.velocity.coords[2] == Approx(-9.81f * kDt).margin(1e-4f));
  CHECK(result.state.position.coords[2] ==
        Approx(10.0f - 0.5f * 9.81f * kDt * kDt).margin(1e-4f));
  CHECK_FALSE(result.contact.has_value());
}

TEST_CASE("Horizontal flight without drag keeps horizontal velocity",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 0.0f});

  Ball ball(config);
  ball.set_state(initial);
  const auto result = ball.AdvanceBallTick({}, kDt, pitch);

  // Gravity changes only the vertical component.
  CHECK(result.state.velocity.coords[0] == Approx(5.0f).margin(1e-6f));
  CHECK(result.state.position.coords[0] == Approx(5.0f * kDt).margin(1e-6f));
}

TEST_CASE("Air drag removes kinetic energy", "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch(football_pitch_model{105.0f, 68.0f, 0.0f, 0.0f, 0.02f});
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {10.0f, 0.0f, 0.0f});

  Ball ball(config);
  ball.set_state(initial);
  const auto result = ball.AdvanceBallTick({}, kDt, pitch);

  CHECK(result.state.velocity.GetLength() < 10.0f);
  CHECK(result.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Spin bends a moving ball sideways (Magnus)", "[unit][ball][simulation]") {
  const BallModel config{0.43f, 0.11f, 0.05f};
  const Pitch pitch = FrictionlessPitch();
  BallState initial = BallAt({0.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 0.0f});
  initial.angular_velocity = Vector3(0.0f, 0.0f, 10.0f);

  Ball ball(config);
  ball.set_state(initial);
  const auto result = ball.AdvanceBallTick({}, kDt, pitch);

  CHECK(result.state.velocity.coords[1] > 0.0f);
  CHECK(result.state.velocity.coords[1] == Approx(0.05f * 50.0f * kDt).margin(1e-4f));
}

TEST_CASE("Grass rolling decelerates horizontal speed", "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch;  // default ground deceleration (1.6 m/s^2)
  const auto initial = BallAt({0.0f, 0.0f, config.radius}, {5.0f, 0.0f, 0.0f});

  Ball ball(config);
  ball.set_state(initial);
  const auto result = ball.AdvanceBallTick({}, kDt, pitch);

  CHECK(result.state.position.coords[2] == Approx(config.radius));
  CHECK(result.state.velocity.coords[0] < 5.0f);
  CHECK(result.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Landing on the ground reflects by the contact restitution",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 0.15f}, {0.0f, 0.0f, -5.0f});
  const std::array<ColliderMotion, 1> colliders{GroundPlane(0.5f)};

  Ball ball(config);
  ball.set_state(initial);
  const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);

  REQUIRE(result.contact.has_value());
  CHECK(result.contact->normal_impulse > 0.0f);
  CHECK(result.state.velocity.coords[2] > 0.0f);
  CHECK(result.state.position.coords[2] > config.radius + 0.001f);
  const auto next = ball.AdvanceBallTick(colliders, kDt, pitch);
  CHECK(next.state.position.coords[2] > result.state.position.coords[2]);
  CHECK_FALSE(next.contact.has_value());
}

TEST_CASE("Initial penetration is repaired without an impulse",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  const auto initial = BallAt({0.0f, 0.0f, 0.05f}, {1.0f, 0.0f, 0.0f});
  const std::array<ColliderMotion, 1> colliders{GroundPlane(0.5f)};

  Ball ball(config);
  ball.set_state(initial);
  const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);

  CHECK(result.state.position.coords[2] == Approx(config.radius).margin(1e-4f));
  CHECK_FALSE(result.contact.has_value());
}

TEST_CASE("Ball Step equals one tick over the pitch colliders",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  Ball ball(config);
  const auto initial = BallAt(
      {pitch.half_length() - 0.5f, -pitch.goal_half_width(), 1.0f},
      {40.0f, 0.0f, 0.0f});
  ball.set_state(initial);

  Ball reference = ball;
  const auto expected = reference.AdvanceBallTick(pitch.colliders(), kDt, pitch);
  const auto actual = ball.Step(pitch);

  REQUIRE(actual.contact.has_value());
  REQUIRE(expected.contact.has_value());
  CHECK(actual.contact->collider == expected.contact->collider);
  CHECK(actual.contact->toi == Approx(expected.contact->toi));
  for (int axis = 0; axis < 3; ++axis) {
    CHECK(actual.state.position.coords[axis] ==
          Approx(expected.state.position.coords[axis]));
    CHECK(actual.state.velocity.coords[axis] ==
          Approx(expected.state.velocity.coords[axis]));
  }
}

TEST_CASE("set_state restores the exact state", "[unit][ball][simulation]") {
  const BallModel config{0.43f, 0.11f, 0.02f};
  Ball ball(config);
  BallState state = BallAt({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
  state.angular_velocity = Vector3(7.0f, 8.0f, 9.0f);

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
  const BallState initial = BallAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
  ball.set_state(initial);

  const Vector3 impulse{6.0f, 0.0f, 0.0f};
  const Vector3 foot{0.0f, 0.0f, 10.0f - config.radius};
  ball.ApplyImpulseAtPoint(impulse, foot);

  const BallState committed = ball.get_state();
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
  const Pitch pitch = FrictionlessPitch();
  Ball ball(config);
  ball.set_state(BallAt({0.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 0.0f}));

  // Side-foot strike on the +x face: +y impulse there spins the ball about +z.
  ball.ApplyImpulseAtPoint(Vector3(0.0f, 0.6f, 0.0f), Vector3(config.radius, 0.0f, 10.0f));
  REQUIRE(ball.get_state().angular_velocity.coords[2] > 0.0f);
  const float before_y = ball.get_state().velocity.coords[1];

  ball.Step(pitch);
  CHECK(ball.get_state().velocity.coords[1] > before_y);
}

TEST_CASE("PlaceOnGround sets ground position and clears motion",
          "[unit][ball][simulation]") {
  const BallModel config{0.43f, 0.11f, 0.02f};
  Ball ball(config);
  ball.set_state(BallAt({1.0f, 2.0f, 10.0f}, {3.0f, 4.0f, 5.0f}));

  ball.PlaceOnGround(7.0f, 8.0f);
  BallState state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(7.0f));
  CHECK(state.position.coords[1] == Approx(8.0f));
  CHECK(state.position.coords[2] == Approx(config.radius));
  CHECK(state.velocity.GetLength() == Approx(0.0f));
  CHECK(state.angular_velocity.GetLength() == Approx(0.0f));
}

TEST_CASE("Ball flies freely through former goal net regions",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  Vector3 position(pitch.half_length() - 0.05f, 0.0f, 1.0f);
  Vector3 velocity(40.0f, 0.0f, 0.0f);
  SECTION("goal line") {}
  SECTION("rear net") {
    position.coords[0] = pitch.half_length() + pitch.goal_depth();
  }
  SECTION("side net") {
    position = Vector3(pitch.half_length() + 1.0f, pitch.goal_half_width(), 1.0f);
    velocity = Vector3(0.0f, 5.0f, 0.0f);
  }
  SECTION("top net") {
    position = Vector3(pitch.half_length() + 1.0f, 0.0f, pitch.goal_height());
    velocity = Vector3(0.0f, 0.0f, 5.0f);
  }
  for (const float side : {-1.0f, 1.0f}) {
    auto initial = BallAt(position, velocity);
    initial.position.coords[0] *= side;
    initial.velocity.coords[0] *= side;
    Ball ball(config);
    ball.set_state(initial);
    const auto result = ball.Step(pitch);
    REQUIRE_FALSE(result.contact.has_value());
    for (int axis = 0; axis < 3; ++axis) {
      const float gravity = axis == 2 ? -9.81f : 0.0f;
      CHECK(result.state.velocity.coords[axis] ==
            Approx(initial.velocity.coords[axis] + gravity * kDt).margin(1e-5f));
      CHECK(result.state.position.coords[axis] ==
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
  REQUIRE(ball.get_state().velocity.coords[2] == Approx(10.0f));
  for (int i = 0; i < 100; ++i) ball.Step(pitch);
  CHECK(ball.get_state().position.coords[2] ==
        Approx(config.radius + 10.0f - 0.5f * 9.81f).margin(0.001f));
  CHECK(ball.get_state().velocity.coords[2] == Approx(10.0f - 9.81f).margin(0.001f));
}

TEST_CASE("Drag-free oblique flight matches analytic trajectory", "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  Ball ball(config);
  ball.set_state(BallAt({0, 0, 2}, {12, 3, 8}));
  for (int i = 0; i < 50; ++i)
    ball.AdvanceBallTick({}, kDt, pitch);
  const auto state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(6.0f).margin(0.001f));
  CHECK(state.position.coords[1] == Approx(1.5f).margin(0.001f));
  CHECK(state.position.coords[2] == Approx(2 + 4 - 0.5f * 9.81f * 0.25f).margin(0.001f));
}

TEST_CASE("Successive ground bounces retain their restitution", "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  for (const float restitution : {0.0f, 0.5f, 1.0f}) {
    INFO("restitution=" << restitution);
    const std::array colliders{GroundPlane(restitution)};
    Ball ball(config);
    ball.set_state(BallAt({0, 0, config.radius}, {0, 0, -2}));
    int bounces = 0;
    for (int i = 0; i < 300; ++i) {
      const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);
      if (result.contact) {
        const auto& contact = *result.contact;
        CHECK(contact.normal_impulse > 0.0f);
        ++bounces;
        const float incoming = contact.relative_velocity.coords[2];
        const float outgoing = incoming + contact.normal_impulse / config.mass;
        CHECK(outgoing == Approx(-restitution * incoming).margin(1e-5f));
      }
      // A bounce can apex and fall again inside the collision-free tail of one
      // tick, so a tick may end at most g/2*tick^2 (0.5 mm) under the grass;
      // the next tick repairs it.
      CHECK(result.state.position.coords[2] >= config.radius - 5e-4f);
    }
    if (restitution == 0) {
      CHECK(bounces == 1);
      CHECK(ball.get_state().velocity.coords[2] == Approx(0));
    } else {
      CHECK(bounces >= 3);
    }
  }
}

TEST_CASE("A tick resolves one impact and flies the rest collision-free",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const HalfSpace right{{0.2f, 0, 0}, {-1, 0, 0}};
  const HalfSpace left{{-0.2f, 0, 0}, {1, 0, 0}};
  std::vector<ColliderMotion> colliders{
      {101, right, right, {1, 0}}, {102, left, left, {1, 0}}};
  SECTION("the second wall is left to the next tick") {
    Ball ball(config);
    ball.set_state(BallAt({0, 0, config.radius}, {30, 0, 0}));
    const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);
    REQUIRE(result.contact.has_value());
    CHECK(result.contact->collider == 101);
    CHECK(result.contact->toi == Approx(0.3f).margin(1e-5f));
    // 0.09 m to the wall, then 0.7 tick of collision-free rolling.
    CHECK(result.state.position.coords[0] ==
          Approx(0.09f - 30.0f * 0.007f).margin(1e-5f));
    CHECK(result.state.velocity.coords[0] == Approx(-30));
    const auto next = ball.AdvanceBallTick(colliders, kDt, pitch);
    REQUIRE(next.contact.has_value());
    CHECK(next.contact->collider == 102);
  }
  SECTION("a kinematic surface velocity is applied when it is the first hit") {
    using football::geometry::Sphere;
    const std::array<ColliderMotion, 1> moving{{
        102,
        Sphere{{-0.35f, 0, config.radius}, 0.1f},
        Sphere{{-0.25f, 0, config.radius}, 0.1f},
        {1, 0}}};
    Ball ball(config);
    ball.set_state(BallAt({0, 0, config.radius}, {-30, 0, 0}));
    const auto result = ball.AdvanceBallTick(moving, kDt, pitch);
    REQUIRE(result.contact.has_value());
    CHECK(result.contact->toi == Approx(0.35f).margin(1e-4f));
    CHECK(result.contact->surface_velocity.coords[0] == Approx(10).margin(1e-4f));
    // Contact at x=-0.105 with the sphere closing at 10 m/s, then 0.65 tick.
    CHECK(result.state.velocity.coords[0] == Approx(50).margin(1e-4f));
    CHECK(result.state.position.coords[0] == Approx(0.22f).margin(1e-4f));
  }
}

TEST_CASE("Small ground launch crosses its apex without a zero-time loop",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const std::array colliders{GroundPlane(1.0f)};
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {0, 0, 0.045f}));
  const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);
  CHECK(result.state.position.coords[2] >= config.radius);
  CHECK(result.state.velocity.coords[2] != Approx(0.045f).margin(0.001f));
}

TEST_CASE("Ground resistance consumes only elapsed time before an impact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const Pitch pitch(football_pitch_model{105.0f, 68.0f, 0.0f, 1.0f, 0.025f});
  const HalfSpace wall{{0.2f, 0, 0}, {-1, 0, 0}};
  const std::array<ColliderMotion, 1> colliders{{{101, wall, wall, {1, 0}}}};
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {30, 0, 0}));
  const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);
  CHECK(result.state.velocity.coords[0] == Approx(-29.99f).margin(1e-4f));
}

TEST_CASE("A single impact per tick hands a fast ball to the next tick",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const HalfSpace right{{0.2f, 0, 0}, {-1, 0, 0}};
  const HalfSpace left{{-0.2f, 0, 0}, {1, 0, 0}};
  const std::array<ColliderMotion, 2> colliders{{
      {101, right, right, {1, 0}}, {102, left, left, {1, 0}}}};
  Ball ball(config);
  ball.set_state(BallAt({0, 0, config.radius}, {10000, 0, 0}));
  const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);
  REQUIRE(result.contact.has_value());
  CHECK(result.contact->collider == 101);
  CHECK(result.contact->toi == Approx(0.0009f).margin(1e-6f));
  // The rest of the tick is collision-free, so the ball ends up past the far
  // wall; the next tick repairs that overlap instead of holding the pose.
  CHECK(result.state.position.coords[0] ==
        Approx(0.09f - 10000.0f * kDt * (1.0f - 0.0009f)).margin(0.05f));
  const auto next = ball.AdvanceBallTick(colliders, kDt, pitch);
  REQUIRE(next.contact.has_value());
  CHECK(next.contact->collider == 102);
  CHECK(next.contact->toi == Approx(0.0f));
  CHECK(next.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Ball Step commits every state component after a collision",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const auto pitch = FrictionlessPitch();
  auto initial = BallAt(
      {pitch.half_length() - 0.5f, -pitch.goal_half_width(), 1.0f},
      {40, 0, 0});
  initial.angular_velocity = Vector3(0, 1, 2);
  initial.orientation.SetAngleAxis(0.3f, {1, 0, 0});
  ball.set_state(initial);

  const auto result = ball.Step(pitch);
  REQUIRE(result.contact.has_value());
  const auto now = ball.get_state();
  CHECK(now.position == result.state.position);
  CHECK(now.velocity == result.state.velocity);
  CHECK(now.angular_velocity == result.state.angular_velocity);
  CHECK_FALSE(now.orientation != result.state.orientation);
  CHECK(now.position.coords[0] != Approx(initial.position.coords[0]));
  CHECK(now.velocity.coords[0] != Approx(initial.velocity.coords[0]));
}

TEST_CASE("AdvanceBallTick commits its returned state and ignores zero duration",
          "[unit][ball][simulation]") {
  Ball ball(BallWithoutAero());
  const auto pitch = FrictionlessPitch();
  ball.set_state(BallAt({1, 2, 3}, {4, 5, 6}));
  const auto result = ball.AdvanceBallTick({}, kDt, pitch);
  const auto now = ball.get_state();
  CHECK(now.position == result.state.position);
  CHECK(now.velocity == result.state.velocity);
  CHECK(now.angular_velocity == result.state.angular_velocity);
  CHECK_FALSE(now.orientation != result.state.orientation);
  const auto zero = ball.AdvanceBallTick({}, 0.0f, pitch);
  CHECK_FALSE(zero.contact.has_value());
  CHECK(ball.get_state().position == now.position);
  CHECK(ball.get_state().velocity == now.velocity);
  CHECK_FALSE(ball.get_state().orientation != now.orientation);
}

TEST_CASE("Overlap repairs stay internal and do not hide the single impact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const HalfSpace left{{0, 0, 0}, {1, 0, 0}};
  const HalfSpace back{{0, 0, 0}, {0, 1, 0}};
  const HalfSpace right{{0.25f, 0, 0}, {-1, 0, 0}};
  const std::array<ColliderMotion, 3> colliders{{
      {101, left, left, {1, 0}},
      {102, back, back, {1, 0}},
      {103, right, right, {1, 0}}}};
  Ball ball(config);

  SECTION("separating overlaps are repaired without reporting an impact") {
    ball.set_state(BallAt({-0.01f, -0.01f, 1}, {1, 1, 0}));
    const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);
    CHECK_FALSE(result.contact.has_value());
    CHECK(result.state.position.coords[0] == Approx(config.radius + kDt));
    CHECK(result.state.position.coords[1] == Approx(config.radius + kDt));
    CHECK(ball.get_state().position == result.state.position);
  }
  SECTION("an impact after two repairs is the only returned contact") {
    ball.set_state(BallAt({-0.01f, -0.01f, 1}, {30, 30, 0}));
    const auto result = ball.AdvanceBallTick(colliders, kDt, pitch);
    REQUIRE(result.contact.has_value());
    CHECK(result.contact->collider == 103);
    CHECK(result.contact->normal_impulse > 0.0f);
    CHECK(result.contact->toi == Approx(0.1f).margin(1e-5f));
    CHECK(result.contact->point.coords[0] == Approx(0.25f).margin(1e-5f));
    CHECK(result.state.velocity.coords[0] == Approx(-30));
    CHECK(ball.get_state().velocity == result.state.velocity);
  }
}
