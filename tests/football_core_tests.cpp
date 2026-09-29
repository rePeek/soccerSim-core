#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/contact/player_contact.hpp"
#include "core/model/ball/ball.hpp"
#include "core/model/player/player.hpp"
#include "core/physics/ball_physics.hpp"
#include "core/physics/player_movement.hpp"

TEST_CASE("Ball owns immutable attributes and commits whole state") {
  football_sim::Ball ball(0.12f, 0.50f, 0.40f);
  REQUIRE(ball.Radius() == Catch::Approx(0.12f));
  REQUIRE(ball.Mass() == Catch::Approx(0.50f));
  REQUIRE(ball.State().position.coords[2] == Catch::Approx(0.12f));

  football_sim::BallState next = ball.State();
  next.position = football_sim::math::Vector3(1.0f, 2.0f, 3.0f);
  next.velocity = football_sim::math::Vector3(4.0f, 0.0f, 0.0f);
  ball.SetState(next);
  REQUIRE(ball.Position().coords[0] == Catch::Approx(1.0f));
  REQUIRE(ball.Speed() == Catch::Approx(4.0f));
}

TEST_CASE("Player id is an immutable profile attribute") {
  football_sim::Player player(42, 1.90f, 80.0f, 0.40f, 0.7f, 0.8f);
  REQUIRE(player.Id() == 42);
  REQUIRE(player.Mass() == Catch::Approx(80.0f));
  REQUIRE(player.BodyRadius() == Catch::Approx(0.40f));

  football_sim::PlayerState next = player.State();
  next.position = football_sim::math::Vector3(3.0f, 0.0f, 0.0f);
  player.SetState(next);
  REQUIRE(player.Position().coords[0] == Catch::Approx(3.0f));
}

TEST_CASE("Ball physics mutates Ball rather than a detached state") {
  football_sim::Ball ball;
  football_sim::BallState next = ball.State();
  next.position = football_sim::math::Vector3(0.0f, 0.0f, 2.0f);
  ball.SetState(next);

  const football_sim::physics::BallPhysicsStepResult result =
      football_sim::physics::BallPhysics::Step(
          ball, 0.01f, false, football_sim::physics::GoalGeometry{});
  REQUIRE(result.impactCount == 0);
  REQUIRE(ball.State().velocity.coords[2] < 0.0f);
  REQUIRE(ball.State().position.coords[2] < 2.0f);
}

TEST_CASE("Player contact updates Player-owned state") {
  football_sim::Player left(1);
  football_sim::Player right(2);
  football_sim::PlayerState leftState = left.State();
  football_sim::PlayerState rightState = right.State();
  leftState.position = football_sim::math::Vector3(-0.1f, 0.0f, 0.0f);
  rightState.position = football_sim::math::Vector3(0.1f, 0.0f, 0.0f);
  left.SetState(leftState);
  right.SetState(rightState);

  const auto contact = football_sim::contact::DetectPlayerContact(left, right);
  REQUIRE(contact.has_value());
  football_sim::contact::ApplyPlayerContactResolution(
      left, right, football_sim::contact::ResolvePlayerContact(left, right, *contact));
  REQUIRE(left.Position().coords[0] < -0.1f);
  REQUIRE(right.Position().coords[0] > 0.1f);
}

TEST_CASE("Authoritative locomotion steps a Player object") {
  football_sim::Player player(7);
  football_sim::physics::PlayerLocomotionInput input;
  input.desiredVelocity = football_sim::math::Vector3(4.0f, 0.0f, 0.0f);
  football_sim::physics::PlayerLocomotionParameters parameters;

  football_sim::physics::PlayerLocomotion::Step(player, input, parameters,
                                                0.01f);
  REQUIRE(player.Velocity().coords[0] > 0.0f);
  REQUIRE(player.Position().coords[0] > 0.0f);
}
