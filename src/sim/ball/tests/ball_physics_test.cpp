#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball.hpp"

namespace {

using football::ball::Ball;
using football::ball::BallEnvironment;
using football::ball::BallState;
using blunted::Vector3;

const football::model::Pitch kPitch = football::model::MakeLegacyPitch();

BallState StateAt(const Vector3& position, const Vector3& velocity) {
  BallState state;
  state.position = position;
  state.velocity = velocity;
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  return state;
}

TEST_CASE("gravity acts on an idle ball", "[sim][ball][physics]") {
  Ball ball(kPitch);
  ball.Reset(StateAt(Vector3(0.0f, 0.0f, 2.0f), Vector3(0.0f, 0.0f, 0.0f)));

  const auto next = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  REQUIRE(next.velocity.coords[2] == Catch::Approx(-0.0981f).margin(1e-3f));
  REQUIRE(next.position.coords[2] < 2.0f);
}

TEST_CASE("ground bounce reflects downward velocity", "[sim][ball][physics]") {
  Ball ball(kPitch);
  ball.Reset(StateAt(Vector3(0.0f, 0.0f, 0.10f), Vector3(0.0f, 0.0f, -1.0f)));

  const auto next = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  REQUIRE(next.velocity.coords[2] > 0.0f);
  REQUIRE(next.position.coords[2] > 0.11f);
}

TEST_CASE("drag and friction reduce horizontal speed", "[sim][ball][physics]") {
  Ball ball(kPitch);
  // On the grass, so ground friction also applies.
  ball.Reset(StateAt(Vector3(0.0f, 0.0f, 0.11f), Vector3(10.0f, 0.0f, 0.0f)));

  const auto next = ball.Predict(football::sim::TickSpan{1}, BallEnvironment{});
  REQUIRE(next.velocity.coords[0] < 10.0f);
}

TEST_CASE("the physics kernel is deterministic", "[sim][ball][physics]") {
  Ball a(kPitch);
  Ball b(kPitch);
  const auto initial = StateAt(Vector3(2.0f, -1.0f, 0.4f), Vector3(4.0f, 2.0f, 3.0f));
  a.Reset(initial);
  b.Reset(initial);

  for (int i = 0; i < 64; ++i) {
    a.Step(football::sim::TickSpan{1}, BallEnvironment{});
    b.Step(football::sim::TickSpan{1}, BallEnvironment{});
  }

  REQUIRE(a.state().position == b.state().position);
  REQUIRE(a.state().velocity == b.state().velocity);
  REQUIRE(a.state().angular_velocity == b.state().angular_velocity);
}

TEST_CASE("Step(0) is a no-op that keeps an accumulated force",
          "[sim][ball][physics]") {
  const auto initial = StateAt(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 0.0f));
  const Vector3 force(10.0f, 0.0f, 0.0f);

  Ball deferred(kPitch);
  deferred.Reset(initial);
  deferred.ApplyForce(force);
  deferred.Step(football::sim::TickSpan{0}, BallEnvironment{});
  REQUIRE(deferred.state().velocity == initial.velocity);
  deferred.Step(football::sim::TickSpan{1}, BallEnvironment{});

  Ball immediate(kPitch);
  immediate.Reset(initial);
  immediate.ApplyForce(force);
  immediate.Step(football::sim::TickSpan{1}, BallEnvironment{});

  REQUIRE(deferred.state().velocity == immediate.state().velocity);
}

TEST_CASE("ApplyForce acts across the whole next Step interval",
          "[sim][ball][physics]") {
  const auto initial = StateAt(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 0.0f));
  const Vector3 force(10.0f, 0.0f, 0.0f);
  const auto dt = football::sim::TickSpan{4};
  const float seconds = football::sim::ToSeconds(dt);

  Ball force_driven(kPitch);
  force_driven.Reset(initial);
  force_driven.ApplyForce(force);
  force_driven.Step(dt, BallEnvironment{});

  // A constant force over dt is equivalent to one impulse of F * dt applied
  // before a Step of the same duration.
  Ball impulse_driven(kPitch);
  impulse_driven.Reset(initial);
  impulse_driven.ApplyImpulse(force * seconds);
  impulse_driven.Step(dt, BallEnvironment{});

  REQUIRE(force_driven.state().position == impulse_driven.state().position);
  REQUIRE(force_driven.state().velocity == impulse_driven.state().velocity);
}

TEST_CASE("ground physics read the injected Pitch surface",
          "[sim][ball][physics]") {
  const auto on_ground = StateAt(Vector3(0.0f, 0.0f, 0.11f), Vector3(10.0f, 0.0f, 0.0f));
  Ball base(kPitch);
  Ball grippy(football::model::Pitch(110.0f, 72.0f, 0.20f, 0.0f, 0.025f));
  base.Reset(on_ground);
  grippy.Reset(on_ground);
  REQUIRE(grippy.Predict(football::sim::TickSpan{1}, BallEnvironment{}).velocity.coords[0] <
          base.Predict(football::sim::TickSpan{1}, BallEnvironment{}).velocity.coords[0]);

  const auto above_ground = StateAt(Vector3(0.0f, 0.0f, 0.16f), Vector3(10.0f, 0.0f, 0.0f));
  Ball short_grass(kPitch);
  Ball tall_grass(football::model::Pitch(110.0f, 72.0f, 0.04f, 1.6f, 0.10f));
  short_grass.Reset(above_ground);
  tall_grass.Reset(above_ground);
  REQUIRE(tall_grass.Predict(football::sim::TickSpan{1}, BallEnvironment{}).velocity.coords[0] <
          short_grass.Predict(football::sim::TickSpan{1}, BallEnvironment{}).velocity.coords[0]);
}

}  // namespace