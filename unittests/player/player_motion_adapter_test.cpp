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

TEST_CASE("KinematicReference preserves action displacement and caps speed",
          "[player][motion][adapter][p6]") {
  PlayerMotionConstraintInput input;
  input.current.position = Vector3(1, 2, 0);
  input.current.velocity = Vector3(0, -3, 0);
  input.reference_delta = Vector3(0.02f, -0.03f, 0);
  input.body = ReferenceBody();
  input.capability = SprintProfile();
  input.capability.max_speed = 4.0f;
  input.authority = PlayerMotionAuthority::KinematicReference;
  const auto result = PlayerMotionAdapter::Step(input);
  CHECK(result.achieved_delta.coords[0] == Catch::Approx(0.02f).margin(1e-6f));
  CHECK(result.achieved_delta.coords[1] == Catch::Approx(-0.03f).margin(1e-6f));
  CHECK(result.state.velocity.GetLength() ==
        Catch::Approx(std::sqrt(0.02f * 0.02f + 0.03f * 0.03f) * 100.0f)
            .margin(1e-4f));
}

// ---------------------------------------------------------------------------
// M1: the constrained result is the only motion authority.
// ---------------------------------------------------------------------------

TEST_CASE("KinematicReference reports the velocity its displacement implies",
          "[player][motion][adapter][m1]") {
  PlayerMotionConstraintInput input;
  input.current.position = Vector3(1, 2, 0);
  input.current.velocity = Vector3(0, -3, 0);
  input.body = ReferenceBody();
  input.capability = SprintProfile();
  input.capability.max_speed = 4.0f;
  input.authority = PlayerMotionAuthority::KinematicReference;

  SECTION("an admissible reference is reproduced exactly") {
    input.reference_delta = Vector3(0.02f, -0.03f, 0);
    const auto result = PlayerMotionAdapter::Step(input);
    CHECK(result.achieved_delta.coords[0] == Catch::Approx(0.02f).margin(1e-6f));
    CHECK(result.achieved_delta.coords[1] == Catch::Approx(-0.03f).margin(1e-6f));
    CHECK_FALSE(result.constrained);
    // Position delta and published velocity describe the same motion.
    CHECK(result.state.position.coords[0] - input.current.position.coords[0] ==
          Catch::Approx(result.achieved_delta.coords[0]).margin(1e-7f));
    CHECK(result.state.position.coords[1] - input.current.position.coords[1] ==
          Catch::Approx(result.achieved_delta.coords[1]).margin(1e-7f));
    CHECK(result.state.velocity.GetLength() * 0.01f ==
          Catch::Approx(result.achieved_delta.GetLength()).margin(1e-7f));
  }

  SECTION("a capped reference publishes the capped velocity, not the intent") {
    // 6 m/s reference against a 4 m/s ceiling: the displacement is clamped, so
    // the velocity that produced it is 4 m/s - never the 6 m/s the clip wanted.
    input.reference_delta = Vector3(0, -0.06f, 0);
    const auto result = PlayerMotionAdapter::Step(input);
    CHECK(result.constrained);
    CHECK(result.achieved_delta.GetLength() ==
          Catch::Approx(0.04f).margin(1e-5f));
    CHECK(result.state.velocity.GetLength() == Catch::Approx(4.0f).margin(1e-4f));
    CHECK(result.state.velocity.GetLength() * 0.01f ==
          Catch::Approx(result.achieved_delta.GetLength()).margin(1e-6f));
    // Direction is preserved by the ceiling.
    CHECK(result.state.velocity.coords[1] < 0.0f);
    CHECK(std::fabs(result.state.velocity.coords[0]) ==
          Catch::Approx(0.0f).margin(1e-5f));
  }

  SECTION("body height scales the reference geometry, not the ceiling") {
    input.reference_delta = Vector3(0, -0.06f, 0);
    input.body.height_scale = 2.0f;
    const auto result = PlayerMotionAdapter::Step(input);
    // Twice the geometry still cannot exceed max_speed * dt.
    CHECK(result.achieved_delta.GetLength() <= 0.04f + 1e-6f);
    CHECK(result.state.velocity.GetLength() <= 4.0f + 1e-4f);
  }
}

TEST_CASE("Locomotion authority ignores the animation reference trajectory",
          "[player][motion][adapter][m1]") {
  // The acceptance rule: changing or disabling animation root XY must not move
  // the Player. With Locomotion authority the adapter derives the trajectory
  // from the control target alone.
  PlayerMotionConstraintInput base;
  base.current.velocity = Vector3(0, -4, 0);
  base.current.bodyFacing = Vector3(0, -1, 0);
  base.desired_velocity = Vector3(0, -4, 0);
  base.idle_facing = Vector3(0, -1, 0);
  base.body = ReferenceBody();
  base.capability = SprintProfile();
  base.authority = PlayerMotionAuthority::Locomotion;

  PlayerMotionConstraintInput with_reference = base;
  with_reference.reference_delta = Vector3(0.0f, -0.04f, 0);

  PlayerMotionConstraintInput without_reference = base;
  without_reference.reference_delta = Vector3(0);

  PlayerMotionConstraintInput wild_reference = base;
  wild_reference.reference_delta = Vector3(12.0f, 9.0f, 0);

  const auto a = PlayerMotionAdapter::Step(with_reference);
  const auto b = PlayerMotionAdapter::Step(without_reference);
  const auto c = PlayerMotionAdapter::Step(wild_reference);

  CHECK(a.achieved_delta.coords[1] == b.achieved_delta.coords[1]);
  CHECK(a.achieved_delta.coords[1] == c.achieved_delta.coords[1]);
  CHECK(a.state.velocity.coords[1] == c.state.velocity.coords[1]);
  CHECK(a.achieved_delta.GetLength() == Catch::Approx(0.04f).margin(1e-6f));
}
