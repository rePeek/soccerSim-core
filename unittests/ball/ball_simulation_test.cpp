// Ball one-tick physics: free motion, drag, spin, grass rolling, landing and
// the Ball::Step/Predict lifecycle.
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
#include "football/ball/ball_contact.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/pitch/pitch.hpp"

using football::math::Vector3;
using football::ball::AdvanceBallTick;
using football::ball::Ball;
using football::ball::BallModel;
using football::ball::BallState;
using football::geometry::ColliderMotion;
using football::geometry::ContactMaterial;
using football::geometry::HalfSpace;
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
BallModel BallWithoutAero() { return BallModel(0.43f, 0.11f, 0.0f); }

// A pitch whose ground does not slow the ball, so gravity/spin tests are pure.
Pitch FrictionlessPitch() { return Pitch(105.0f, 68.0f, 0.0f, 0.0f, 0.0f); }

ColliderMotion GroundPlane(float restitution) {
  const HalfSpace plane{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
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
  const BallModel config(0.43f, 0.11f, 0.0f);
  const Pitch pitch = Pitch(105.0f, 68.0f, 0.0f, 0.0f, 0.02f);
  const auto initial = BallAt({0.0f, 0.0f, 10.0f}, {10.0f, 0.0f, 0.0f});

  const auto result = AdvanceBallTick(initial, {}, kDt, config, pitch);

  CHECK(result.state.velocity.GetLength() < 10.0f);
  CHECK(result.state.velocity.coords[0] > 0.0f);
}

TEST_CASE("Spin bends a moving ball sideways (Magnus)", "[unit][ball][simulation]") {
  const BallModel config(0.43f, 0.11f, 0.05f);
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
  CHECK(result.state.position.coords[2] > config.radius() + 0.001f);
  const auto next = AdvanceBallTick(result.state, colliders, kDt, config, pitch);
  CHECK(next.state.position.coords[2] > result.state.position.coords[2]);
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

TEST_CASE("Ball Predict previews without mutating state",
          "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball ball(config, pitch);
  ball.set_state(BallAt({0.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 0.0f}));
  const BallState before = ball.get_state();

  const auto preview = ball.Predict();

  CHECK(ball.get_state().position.coords[0] == Approx(before.position.coords[0]));
  CHECK(ball.get_state().velocity.coords[2] == Approx(before.velocity.coords[2]));
  CHECK(preview.state.position.coords[0] != Approx(before.position.coords[0]));
}

TEST_CASE("Ball Step commits exactly what Predict previewed",
          "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball preview_ball(config, pitch);
  Ball commit_ball(config, pitch);
  const auto initial = BallAt({0.0f, 0.0f, 5.0f}, {2.0f, 0.0f, 0.0f});
  preview_ball.set_state(initial);
  commit_ball.set_state(initial);

  const auto preview = preview_ball.Predict();
  const auto committed = commit_ball.Step();

  CHECK(preview.state.position.coords[0] == Approx(committed.state.position.coords[0]));
  CHECK(preview.state.position.coords[2] == Approx(committed.state.position.coords[2]));
  CHECK(preview.state.velocity.coords[2] == Approx(committed.state.velocity.coords[2]));
  CHECK(commit_ball.get_state().position.coords[0] ==
        Approx(committed.state.position.coords[0]));
}

TEST_CASE("Ball Step equals one tick over the pitch colliders",
          "[unit][ball][simulation]") {
  const BallModel config = BallWithoutAero();
  const Pitch pitch = FrictionlessPitch();
  Ball ball(config, pitch);
  const auto initial = BallAt(
      {pitch.half_length() - 0.5f, -pitch.goal_half_width(), 1.0f},
      {40.0f, 0.0f, 0.0f});
  ball.set_state(initial);

  const auto expected =
      AdvanceBallTick(initial, pitch.colliders(), kDt, config, pitch);
  const auto actual = ball.Step();

  REQUIRE(!actual.contacts.empty());
  REQUIRE(actual.contacts.size() == expected.contacts.size());
  for (std::size_t i = 0; i < actual.contacts.size(); ++i) {
    CHECK(actual.contacts[i].collider == expected.contacts[i].collider);
    CHECK(actual.contacts[i].toi == Approx(expected.contacts[i].toi));
  }
  for (int axis = 0; axis < 3; ++axis) {
    CHECK(actual.state.position.coords[axis] ==
          Approx(expected.state.position.coords[axis]));
    CHECK(actual.state.velocity.coords[axis] ==
          Approx(expected.state.velocity.coords[axis]));
  }
}

TEST_CASE("Ball Predict over a horizon does not mutate state",
          "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball ball(config, pitch);
  ball.set_state(BallAt({0.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 0.0f}));
  const BallState before = ball.get_state();

  const BallState future = ball.Predict(football::sim::TickSpan{10});

  CHECK(ball.get_state().position.coords[0] == Approx(before.position.coords[0]));
  CHECK(future.position.coords[2] < before.position.coords[2]);
}

TEST_CASE("Ball Reset restores the exact state", "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball ball(config, pitch);
  BallState state = BallAt({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
  state.angular_velocity = Vector3(7.0f, 8.0f, 9.0f);

  ball.set_state(state);

  CHECK(ball.get_state().position.coords[0] == Approx(1.0f));
  CHECK(ball.get_state().position.coords[2] == Approx(3.0f));
  CHECK(ball.get_state().velocity.coords[1] == Approx(5.0f));
  CHECK(ball.get_state().angular_velocity.coords[2] == Approx(9.0f));
}

TEST_CASE("PlaceOnGround sets ground position and clears motion",
          "[unit][ball][simulation]") {
  const BallModel config;
  const Pitch pitch;
  Ball ball(config, pitch);
  ball.set_state(BallAt({1.0f, 2.0f, 10.0f}, {3.0f, 4.0f, 5.0f}));

  ball.PlaceOnGround(7.0f, 8.0f);
  BallState state = ball.get_state();
  CHECK(state.position.coords[0] == Approx(7.0f));
  CHECK(state.position.coords[1] == Approx(8.0f));
  CHECK(state.position.coords[2] == Approx(config.radius()));
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
    Ball ball(config, pitch);
    ball.set_state(initial);
    const auto preview = ball.Predict();
    const auto future = ball.Predict(football::sim::TickSpan{10});
    const auto result = ball.Step();
    REQUIRE(result.contacts.empty());
    for (int axis = 0; axis < 3; ++axis) {
      const float gravity = axis == 2 ? -9.81f : 0.0f;
      CHECK(result.state.velocity.coords[axis] ==
            Approx(initial.velocity.coords[axis] + gravity * kDt).margin(1e-5f));
      CHECK(result.state.position.coords[axis] ==
            Approx(initial.position.coords[axis] + initial.velocity.coords[axis] * kDt +
                   0.5f * gravity * kDt * kDt).margin(1e-5f));
      CHECK(preview.state.velocity.coords[axis] == result.state.velocity.coords[axis]);
      CHECK(preview.state.position.coords[axis] == result.state.position.coords[axis]);
      CHECK(future.velocity.coords[axis] ==
            Approx(initial.velocity.coords[axis] + gravity * 10 * kDt).margin(1e-5f));
    }
  }
}

TEST_CASE("Ground kick launches on subsequent ticks", "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  Ball ball(config, pitch);
  ball.set_state(BallAt({0, 0, config.radius()}, {0, 0, 10}));
  REQUIRE(ball.get_state().velocity.coords[2] == Approx(10.0f));
  for (int i = 0; i < 100; ++i) ball.Step();
  CHECK(ball.get_state().position.coords[2] ==
        Approx(config.radius() + 10.0f - 0.5f * 9.81f).margin(0.001f));
  CHECK(ball.get_state().velocity.coords[2] == Approx(10.0f - 9.81f).margin(0.001f));
}

TEST_CASE("Drag-free oblique flight matches analytic trajectory", "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  auto state = BallAt({0, 0, 2}, {12, 3, 8});
  for (int i = 0; i < 50; ++i)
    state = AdvanceBallTick(state, {}, kDt, config, pitch).state;
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
    auto state = BallAt({0, 0, config.radius()}, {0, 0, -2});
    int bounces = 0;
    for (int i = 0; i < 300; ++i) {
      const auto result = AdvanceBallTick(state, colliders, kDt, config, pitch);
      for (const auto& contact : result.contacts) {
        if (contact.normal_impulse <= 0) continue;
        ++bounces;
        const float incoming = contact.relative_velocity.coords[2];
        const float outgoing = incoming + contact.normal_impulse / config.mass();
        CHECK(outgoing == Approx(-restitution * incoming).margin(1e-5f));
      }
      CHECK(result.state.position.coords[2] >= config.radius() - 1e-5f);
      state = result.state;
    }
    if (restitution == 0) {
      CHECK(bounces == 1);
      CHECK(state.velocity.coords[2] == Approx(0));
    } else {
      CHECK(bounces >= 3);
    }
  }
}

TEST_CASE("CCD integrates two impacts and the remaining tick", "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const HalfSpace right{{0.2f, 0, 0}, {-1, 0, 0}};
  const HalfSpace left{{-0.2f, 0, 0}, {1, 0, 0}};
  std::vector<ColliderMotion> colliders{
      {101, right, right, {1, 0}}, {102, left, left, {1, 0}}};
  SECTION("static walls") {
    const auto result = AdvanceBallTick(
        BallAt({0, 0, config.radius()}, {30, 0, 0}), colliders, kDt, config, pitch);
    REQUIRE(result.contacts.size() == 2);
    CHECK(result.contacts[0].toi == Approx(0.3f).margin(1e-5f));
    CHECK(result.contacts[1].toi == Approx(0.9f).margin(1e-5f));
    CHECK(result.state.position.coords[0] == Approx(-0.06f).margin(1e-5f));
    CHECK(result.state.velocity.coords[0] == Approx(30));
  }
  SECTION("moving sphere uses only its remaining motion interval") {
    using football::geometry::Sphere;
    colliders[1] = {102, Sphere{{-0.35f, 0, config.radius()}, 0.1f},
                         Sphere{{-0.25f, 0, config.radius()}, 0.1f}, {1, 0}};
    const auto result = AdvanceBallTick(
        BallAt({0, 0, config.radius()}, {30, 0, 0}), colliders, kDt, config, pitch);
    std::vector<football::ball::BallContact> impacts;
    for (const auto& c : result.contacts) if (c.normal_impulse > 0) impacts.push_back(c);
    REQUIRE(impacts.size() == 2);
    CHECK(impacts[1].toi == Approx(0.8f).margin(1e-5f));
    CHECK(impacts[1].surface_velocity.coords[0] == Approx(10).margin(1e-4f));
    CHECK(result.state.position.coords[0] == Approx(0.04f).margin(1e-4f));
    CHECK(result.state.velocity.coords[0] == Approx(50).margin(1e-4f));
  }
}

TEST_CASE("Small ground launch crosses its apex without a zero-time loop",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const std::array colliders{GroundPlane(1.0f)};
  const auto result = AdvanceBallTick(
      BallAt({0, 0, config.radius()}, {0, 0, 0.045f}),
      colliders, kDt, config, pitch);
  CHECK(result.state.position.coords[2] >= config.radius());
  CHECK(result.state.velocity.coords[2] != Approx(0.045f).margin(0.001f));
}

TEST_CASE("Ground resistance consumes only elapsed time before an impact",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const Pitch pitch(105, 68, 0, 1, 0.025f);
  const HalfSpace wall{{0.2f, 0, 0}, {-1, 0, 0}};
  const std::array<ColliderMotion, 1> colliders{{{101, wall, wall, {1, 0}}}};
  const auto result = AdvanceBallTick(
      BallAt({0, 0, config.radius()}, {30, 0, 0}),
      colliders, kDt, config, pitch);
  CHECK(result.state.velocity.coords[0] == Approx(-29.99f).margin(1e-4f));
}

TEST_CASE("Impact cap holds the last safe pose rather than tunnelling",
          "[unit][ball][simulation]") {
  const auto config = BallWithoutAero();
  const auto pitch = FrictionlessPitch();
  const HalfSpace right{{0.2f, 0, 0}, {-1, 0, 0}};
  const HalfSpace left{{-0.2f, 0, 0}, {1, 0, 0}};
  const std::array<ColliderMotion, 2> colliders{{
      {101, right, right, {1, 0}}, {102, left, left, {1, 0}}}};
  const auto result = AdvanceBallTick(
      BallAt({0, 0, config.radius()}, {10000, 0, 0}),
      colliders, kDt, config, pitch);
  REQUIRE(result.contacts.size() == 16);
  CHECK(result.state.position.coords[0] >= -0.09001f);
  CHECK(result.state.position.coords[0] <= 0.09001f);
  CHECK(result.contacts.back().toi < 1.0f);
}

