#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "football/ball/ball.hpp"
#include "football/ball/ball_response.hpp"

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

TEST_CASE("penetration repair uses a spherical surface arm and reports response impulses",
          "[ball][tick-input][response]") {
  const football::model::BallConfig config;
  BallDynamics no_forces;
  no_forces.gravity = no_forces.quadratic_resistance = 0;
  no_forces.ground_deceleration = 0;
  ColliderMotion c{1000, Sphere{{0, 0, 1}, .2f}, Sphere{{0, 0, 1}, .2f}, {.35f, .45f}};
  BallState initial{{.1f, 0, 1}, {-5, 2, 0}, Vector3(0), blunted::Quaternion{}};
  const auto result = AdvanceBallTick(initial, std::span{&c, 1}, .01f, config, no_forces);
  REQUIRE(result.contacts.size() == 1);
  const auto& hit = result.contacts.front();
  REQUIRE(hit.toi == 0);
  REQUIRE(hit.position_corrected);
  REQUIRE(hit.point == result.state.position);
  REQUIRE(hit.point.coords[0] == Catch::Approx(.31f));
  REQUIRE(hit.normal_impulse == Catch::Approx(config.mass() * 1.35f * 5));
  const float jt = -2 / (1 / config.mass() + config.radius() * config.radius() / BallInertia(config));
  REQUIRE(result.state.angular_velocity.coords[2] == Catch::Approx(-config.radius() * jt / BallInertia(config)));

  initial.velocity = {-5, 0, 0};
  const auto normal = AdvanceBallTick(initial, std::span{&c, 1}, .01f, config, no_forces);
  REQUIRE(normal.state.angular_velocity.GetLength() < 1e-6f);
  // Projected/separating overlap is evidence but not an impulse-bearing hit.
  initial.velocity = {5, 0, 0};
  const auto separating = AdvanceBallTick(initial, std::span{&c, 1}, .01f, config, no_forces);
  REQUIRE(separating.contacts.front().normal_impulse == 0);
  REQUIRE(separating.contacts.front().position_corrected);
  REQUIRE(separating.state.velocity == initial.velocity);
  // Once separated and moving away, next tick has no body contact.
  REQUIRE(AdvanceBallTick(separating.state, std::span{&c, 1}, .01f, config, no_forces).contacts.empty());
}

TEST_CASE("response ignores stale contact center and respects friction cap",
          "[ball][tick-input][response]") {
  const football::model::BallConfig config;
  const BallState ball{{4, 5, 1}, {-10, 8, 0}, Vector3(0), blunted::Quaternion{}};
  BallContact hit{1000, 0, Vector3(-20, 10, 0), Vector3(1, 0, 0), Vector3(0)};
  const auto response = ResolveContactResponse(ball, hit, {.35f, .01f}, Vector3(0), config);
  REQUIRE(response.normal_impulse == Catch::Approx(1.35f * 10 * config.mass()));
  REQUIRE(response.tangential_impulse.GetLength() == Catch::Approx(.01f * response.normal_impulse));
  REQUIRE(response.state.angular_velocity.coords[2] > 0);
  hit.point = ball.position;
  SameState(response.state, ResolveContact(ball, hit, {.35f, .01f}, Vector3(0), config));
}

TEST_CASE("active endpoint impulse changes velocity and spin without advancing position",
          "[ball][tick-input][active]") {
  const football::model::Pitch pitch;
  const football::model::BallConfig config;
  const BallState initial{{0, 0, .11f}, {3, -1, 0}, Vector3(0), blunted::Quaternion{}};
  for (bool on_ground : {true, false}) {
    BallState start = initial;
    if (!on_ground) start.position.coords[2] = 1.2f;
    Ball plain(pitch), active(pitch);
    plain.Reset(start);
    active.Reset(start);
    const auto baseline = plain.Step(BallTickInput{{}, {}});
    REQUIRE_FALSE(baseline.active_impulse.has_value());
    const Vector3 point = start.position + Vector3(.11f, 0, 0);
    const Vector3 impulse{5, 0, 2};
    const auto with_active = active.Step(BallTickInput{{}, {}, BallImpulse{impulse, point}});
    REQUIRE(with_active.active_impulse.has_value());
    // Position is the passive endpoint: the impulse never advances position.
    REQUIRE(with_active.state.position == baseline.state.position);
    REQUIRE(with_active.state.orientation.elements[0] == baseline.state.orientation.elements[0]);
    // Linear change is exactly J / mass; spin appears because the point is offset.
    REQUIRE((with_active.state.velocity - baseline.state.velocity - impulse / config.mass())
                .GetLength() < 1e-6f);
    REQUIRE(with_active.state.angular_velocity.coords[1] < 0);
    // A second plain Step after the impulse reproduces the same physics from the
    // new state: the impulse is state, not an out-of-band action.
    Ball expected(pitch);
    expected.Reset(with_active.state);
    const auto replay = expected.Step(BallTickInput{{}, {}});
    const auto repeated = active.Step(BallTickInput{{}, {}});
    SameState(replay.state, repeated.state);
    REQUIRE_FALSE(repeated.active_impulse.has_value());

    // Mirror symmetry: mirrored input yields the mirrored impulse response.
    BallState mirrored = start;
    mirrored.position.Mirror();
    mirrored.velocity.Mirror();
    Ball mirrored_ball(pitch);
    mirrored_ball.Reset(mirrored);
    Vector3 mirrored_impulse = impulse, mirrored_point = point;
    mirrored_impulse.Mirror();
    mirrored_point.Mirror();
    const auto mirrored_result =
        mirrored_ball.Step(BallTickInput{{}, {}, BallImpulse{mirrored_impulse, mirrored_point}});
    Vector3 expected_velocity = with_active.state.velocity;
    expected_velocity.Mirror();
    REQUIRE((mirrored_result.state.velocity - expected_velocity).GetLength() < 1e-6f);
  }
}

TEST_CASE("tick preview is read only and matches commit including forces and constraints", "[ball][tick-input]") {
  Ball ball(football::model::Pitch{});
  ball.Reset({{0, 0, .11f}, {20, 0, 0}, {0, 0, 2}, blunted::Quaternion{}});
  const auto initial = ball.state();
  const std::array body{MovingBody(100, .4f, -1)};
  ball.ApplyForce({1, 2, 0});
  BallTickInput input{body, {}};
  const auto preview = ball.Predict(input);
  SameState(ball.state(), initial);
  SameState(ball.Predict(input).state, preview.state);
  SameState(ball.Step(input).state, preview.state);
  REQUIRE_FALSE(preview.contacts.empty());
  input.endpoint_constraint = BallEndpointConstraint{{1, 2, .8f}, {0, 1, 0}};
  const auto constrained = ball.Predict(input);
  REQUIRE(constrained.state.position == Vector3(1, 2, .8f));
  REQUIRE(constrained.state.velocity == Vector3(0, 1, 0));
  REQUIRE(constrained.state.angular_velocity == Vector3(0));
  SameState(ball.Step(input).state, constrained.state);
  input.active_impulse = BallImpulse{{1, 0, 0}, {1, 2, .8f}};
  const auto before_failure = ball.state();
  REQUIRE_THROWS_AS(ball.Predict(input), std::invalid_argument);
  REQUIRE_THROWS_AS(ball.Step(input), std::invalid_argument);
  SameState(ball.state(), before_failure);
}
