// P2 — pure PlayerMotionAdapter contract tests. No Animation, Ball, Runtime,
// collision or assets: reference delta + capability in, constrained root out.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/player/execution/player_motion.hpp"
#include "football/player/execution/player_motion_adapter.hpp"

using football::math::Vector3;

namespace {

PlayerMotionProfile SprintProfile() {
  return PlayerMotionProfile{8.0f, 6.0f, 12.0f, 6.0f};
}

football::player::PlayerBodyProfile ReferenceBody() {
  return football::player::PlayerBodyProfile{1.92f, 1.0f, 1.0f};
}

PlayerMotionConstraintInput Input(const Vector3& reference_delta,
                                  const Vector3& current_velocity,
                                  const Vector3& desired_velocity,
                                  PlayerMotionAuthority authority) {
  PlayerMotionConstraintInput input;
  input.reference_delta = reference_delta;
  input.current.velocity = current_velocity;
  input.current.bodyFacing = Vector3(0, -1, 0);
  input.body = ReferenceBody();
  input.capability = SprintProfile();
  input.desired_velocity = desired_velocity;
  input.authority = authority;
  input.dt = 0.01f;
  return input;
}

}  // namespace

TEST_CASE("adapter identity reproduces the reference displacement",
          "[player][motion][adapter][p2]") {
  const Vector3 velocity(0, -6.0f, 0);
  const auto result = PlayerMotionAdapter::Step(Input(
      velocity * 0.01f, velocity, velocity, PlayerMotionAuthority::ReferenceAction));
  CHECK(result.achieved_delta.coords[0] == Catch::Approx(0.0f).margin(1e-6f));
  CHECK(result.achieved_delta.coords[1] == Catch::Approx(-0.06f).margin(1e-5f));
  CHECK(result.state.velocity.GetLength() == Catch::Approx(6.0f).margin(1e-4f));
  CHECK_FALSE(result.constrained);
}

TEST_CASE("adapter enforces the acceleration limit",
          "[player][motion][adapter][p2]") {
  const auto result = PlayerMotionAdapter::Step(Input(
      Vector3(0, -0.08f, 0), Vector3(0), Vector3(0),
      PlayerMotionAuthority::ReferenceAction));
  CHECK(result.state.velocity.GetLength() == Catch::Approx(0.06f).margin(1e-5f));
  CHECK(result.achieved_delta.GetLength() == Catch::Approx(0.0006f).margin(1e-6f));
  CHECK(result.constrained);
}

TEST_CASE("adapter enforces the speed ceiling before acceleration",
          "[player][motion][adapter][p2]") {
  auto input = Input(Vector3(0, -0.10f, 0), Vector3(0, -7.0f, 0), Vector3(0),
                     PlayerMotionAuthority::ReferenceAction);
  const auto result = PlayerMotionAdapter::Step(input);
  CHECK(result.state.velocity.GetLength() <= 8.0f + 1e-4f);
  CHECK(result.state.velocity.GetLength() == Catch::Approx(7.06f).margin(1e-4f));
}

TEST_CASE("adapter brakes toward a zero reference",
          "[player][motion][adapter][p2]") {
  const auto result = PlayerMotionAdapter::Step(Input(
      Vector3(0), Vector3(0, -6.0f, 0), Vector3(0),
      PlayerMotionAuthority::ReferenceAction));
  CHECK(result.state.velocity.GetLength() == Catch::Approx(5.88f).margin(1e-4f));
  CHECK(result.achieved_delta.GetLength() == Catch::Approx(0.0588f).margin(1e-5f));
  CHECK(result.constrained);
}

TEST_CASE("adapter scales reference distance by body size, not capability",
          "[player][motion][adapter][p2]") {
  auto input = Input(Vector3(0, -0.06f, 0), Vector3(0, -6.3f, 0), Vector3(0),
                     PlayerMotionAuthority::ReferenceAction);
  input.body.height_scale = 1.05f;
  const auto result = PlayerMotionAdapter::Step(input);
  CHECK(result.achieved_delta.coords[1] == Catch::Approx(-0.063f).margin(1e-5f));
  CHECK(result.state.velocity.GetLength() == Catch::Approx(6.3f).margin(1e-4f));
  CHECK_FALSE(result.constrained);
}

TEST_CASE("adapter is deterministic for identical inputs",
          "[player][motion][adapter][p2]") {
  const auto input = Input(Vector3(0, -0.06f, 0), Vector3(0, -5.0f, 0),
                           Vector3(0, -6.0f, 0),
                           PlayerMotionAuthority::ReferenceAction);
  const auto first = PlayerMotionAdapter::Step(input);
  const auto second = PlayerMotionAdapter::Step(input);
  CHECK(first.state.position.coords[0] == second.state.position.coords[0]);
  CHECK(first.state.position.coords[1] == second.state.position.coords[1]);
  CHECK(first.achieved_delta.coords[1] == second.achieved_delta.coords[1]);
  CHECK(first.constrained == second.constrained);
}

TEST_CASE("PlayerMotion delegates locomotion to the adapter core",
          "[player][motion][adapter][p4]") {
  PlayerKinematicState current;
  current.velocity = Vector3(0);
  current.bodyFacing = Vector3(0, -1, 0);
  auto profile = SprintProfile();
  const MotionStep step = PlayerMotion::StepContinuous(
      current, profile, Vector3(0, -8.0f, 0), Vector3(0, -1, 0), 0.01f);
  CHECK(step.state.velocity.GetLength() == Catch::Approx(0.06f).margin(1e-5f));
  CHECK(step.sample.displacement.GetLength() ==
        Catch::Approx(0.0006f).margin(1e-6f));
  CHECK(step.sample.velocity.GetLength() ==
        Catch::Approx(step.state.velocity.GetLength()));
}
