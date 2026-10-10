#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "football/ball/ball.hpp"

using namespace football::ball;
using blunted::Vector3;
using football::sim::TickSpan;

namespace {
void SameState(const BallState& a, const BallState& b) {
  REQUIRE(a.position == b.position);
  REQUIRE(a.velocity == b.velocity);
  REQUIRE(a.angular_velocity == b.angular_velocity);
  for (int i = 0; i < 4; ++i) REQUIRE(a.orientation.elements[i] == b.orientation.elements[i]);
}
ColliderMotion MovingBody(ColliderId id, float x, float vx) {
  ColliderMotion c;
  c.id = id;
  c.start = Capsule{{x, 0, .05f}, {x, 0, 1.15f}, .19f};
  c.end = Capsule{{x + vx * .01f, 0, .05f}, {x + vx * .01f, 0, 1.15f}, .19f};
  c.material = {.5f, .0f};
  return c;
}
}

TEST_CASE("empty tick input preserves static prediction and duration adapter",
          "[ball][tick-input]") {
  const football::model::Pitch pitch;
  for (Vector3 position : {Vector3(0, 0, .11f), Vector3(0, 0, 2),
                           Vector3(54.5f, pitch.goal_half_width(), 1),
                           Vector3(58.5f, 0, 1)}) {
    for (bool net : {false, true}) {
      Ball old(pitch), current(pitch);
      BallState initial{position, Vector3(20, 1, -4), Vector3(3, 8, 4), blunted::Quaternion{}};
      old.Reset(initial);
      current.Reset(initial);
      for (int i = 0; i < 40; ++i) {
        const auto predicted = current.Predict(TickSpan{1}, BallEnvironment{net});
        old.Step(TickSpan{1}, BallEnvironment{net});
        const auto result = current.Step(BallTickInput{{}, BallEnvironment{net}});
        SameState(old.state(), result.state);
        SameState(predicted, result.state);
        SameState(current.state(), result.state);
        REQUIRE(result.contacts.size() <= 1);
      }
    }
  }
}

TEST_CASE("tick input resolves moving bodies and returns first contact without remainder",
          "[ball][tick-input]") {
  Ball ball(football::model::Pitch{});
  BallState initial{{0, 0, .6f}, {50, 0, 0}, Vector3(0), blunted::Quaternion{}};
  ball.Reset(initial);
  std::array colliders{MovingBody(1000, .7f, -20)};
  const auto result = ball.Step(BallTickInput{colliders, {}});
  REQUIRE(result.contacts.size() == 1);
  const auto& hit = result.contacts.front();
  REQUIRE(hit.collider == 1000);
  REQUIRE(hit.toi > 0);
  REQUIRE(hit.toi < 1);
  REQUIRE(result.state.position == hit.point);
  REQUIRE(result.state.velocity.coords[0] < 0);
  REQUIRE(hit.relative_velocity.coords[0] > 69);
  SameState(result.state, ball.state());

  // Step input is borrowed for this tick only. Predictions must not assume
  // this body's motion continues into any future tick or retain caller spans.
  Ball reference(football::model::Pitch{});
  reference.Reset(result.state);
  SameState(ball.Predict(TickSpan{10}, {}), reference.Predict(TickSpan{10}, {}));
  const auto next = ball.Step(BallTickInput{{}, {}});
  REQUIRE(next.contacts.empty());
}

TEST_CASE("static and dynamic candidates compete in one earliest-contact world",
          "[ball][tick-input]") {
  const football::model::Pitch pitch;
  Ball ball(pitch);
  // Post lies before a body beyond the goal line.
  BallState initial{{pitch.half_length() - .5f, pitch.goal_half_width(), 1},
                    {100, 0, 0}, Vector3(0), blunted::Quaternion{}};
  auto body = MovingBody(1000, pitch.half_length() + .7f, 0);
  std::get<Capsule>(body.start).a.coords[1] = pitch.goal_half_width();
  std::get<Capsule>(body.start).b.coords[1] = pitch.goal_half_width();
  body.end = body.start;
  ball.Reset(initial);
  const auto post = ball.Step(BallTickInput{std::span{&body, 1}, {}});
  REQUIRE(post.contacts.size() == 1);
  REQUIRE(post.contacts.front().collider <= 7);

  ball.Reset(BallState{{0, 0, .2f}, {0, 0, -50}, Vector3(0), blunted::Quaternion{}});
  body = MovingBody(1000, .7f, -50);
  const auto ground = ball.Step(BallTickInput{std::span{&body, 1}, {}});
  REQUIRE(ground.contacts.size() == 1);
  REQUIRE(ground.contacts.front().collider == 1);
}

TEST_CASE("dynamic collider order is irrelevant and ties use stable ids",
          "[ball][tick-input]") {
  Ball ball(football::model::Pitch{});
  const BallState initial{{0, 0, .6f}, {100, 0, 0}, Vector3(0), blunted::Quaternion{}};
  for (bool tie : {false, true}) {
    std::array bodies{MovingBody(1002, .7f, 0), MovingBody(1001, tie ? .7f : .9f, 0)};
    ball.Reset(initial);
    const auto a = ball.Step(BallTickInput{bodies, {}});
    std::reverse(bodies.begin(), bodies.end());
    ball.Reset(initial);
    const auto b = ball.Step(BallTickInput{bodies, {}});
    SameState(a.state, b.state);
    REQUIRE(a.contacts.front().collider == b.contacts.front().collider);
    REQUIRE(a.contacts.front().collider == (tie ? 1001 : 1002));
  }
}

TEST_CASE("invalid contact identity cannot mutate state or consume pending force",
          "[ball][tick-input]") {
  Ball ball(football::model::Pitch{}), reference(football::model::Pitch{});
  const BallState initial{{0, 0, 2}, Vector3(0), Vector3(0), blunted::Quaternion{}};
  for (ColliderId id : {0u, 1u, 7u, 1000u}) {
    ball.Reset(initial);
    reference.Reset(initial);
    ball.ApplyForce({1, 0, 0});
    reference.ApplyForce({1, 0, 0});
    std::array bodies{MovingBody(id, 10, 0), MovingBody(id, 20, 0)};
    REQUIRE_THROWS_AS(ball.Step(BallTickInput{bodies, {}}), std::invalid_argument);
    SameState(ball.state(), initial);
    SameState(ball.Step(BallTickInput{{}, {}}).state,
              reference.Step(BallTickInput{{}, {}}).state);
  }
}
