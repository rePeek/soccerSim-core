// P3b core: pure support-anchor and two-bone IK contract.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/player/execution/player_foot_ik.hpp"
#include "player/execution/player_baked_body_pose.hpp"

using football::math::Vector3;

TEST_CASE("two-bone IK preserves segment lengths and reaches the target",
          "[player][footik][p3b]") {
  const Vector3 hip(0, 0, 1.0f);
  const Vector3 knee(0, 0, 0.5f);
  const Vector3 ankle(0, 0, 0.0f);
  const Vector3 target(0.2f, 0.0f, 0.3f);
  const auto result = PlayerFootIk::Solve(hip, knee, ankle, target);
  CHECK(result.reachable);
  CHECK_FALSE(result.released);
  CHECK((result.knee - hip).GetLength() == Catch::Approx(0.5f).margin(1e-5f));
  CHECK((result.ankle - result.knee).GetLength() ==
        Catch::Approx(0.5f).margin(1e-5f));
  CHECK((result.ankle - target).GetLength() <= 1e-5f);
}

TEST_CASE("two-bone IK releases an unreachable anchor",
          "[player][footik][p3b]") {
  const Vector3 hip(0, 0, 1.0f);
  const Vector3 knee(0, 0, 0.5f);
  const Vector3 ankle(0, 0, 0.0f);
  const auto result =
      PlayerFootIk::Solve(hip, knee, ankle, Vector3(0, 0, -2.0f));
  CHECK(result.reachable);
  CHECK(result.released);
  CHECK((result.ankle - hip).GetLength() <= 1.0f + 1e-4f);
  CHECK((result.ankle - result.knee).GetLength() ==
        Catch::Approx(0.5f).margin(1e-5f));
}

TEST_CASE("foot plant anchors persist until support or ground is lost",
          "[player][footik][p3b]") {
  PlayerFootPlantInput input;
  input.left_support = true;
  input.left_ankle = Vector3(0.1f, 0.2f, 0.1f);
  auto state = PlayerFootIk::Update(PlayerFootPlantState{}, input);
  REQUIRE(state.left_planted);
  CHECK(state.left_phase == FootContactPhase::Planting);
  const Vector3 anchor = state.left_anchor;
  CHECK(anchor == input.left_ankle);

  input.left_ankle = Vector3(0.2f, 0.3f, 0.1f);
  state = PlayerFootIk::Update(state, input);
  CHECK(state.left_anchor == anchor);
  CHECK(state.left_phase == FootContactPhase::Planted);

  input.left_support = false;
  state = PlayerFootIk::Update(state, input);
  CHECK_FALSE(state.left_planted);
  CHECK(state.left_phase == FootContactPhase::Releasing);

  input.left_support = true;
  input.left_ankle = Vector3(0.0f, 0.0f, 0.5f);
  state = PlayerFootIk::Update(state, input);
  CHECK_FALSE(state.left_planted);
  CHECK(state.left_phase == FootContactPhase::Swing);
}

TEST_CASE("foot plant update is deterministic", "[player][footik][p3b]") {
  PlayerFootPlantInput input;
  input.right_support = true;
  input.right_ankle = Vector3(-0.1f, 0.0f, 0.05f);
  const auto first = PlayerFootIk::Update(PlayerFootPlantState{}, input);
  const auto second = PlayerFootIk::Update(PlayerFootPlantState{}, input);
  CHECK(first.right_anchor == second.right_anchor);
  CHECK(first.right_planted == second.right_planted);
}

TEST_CASE("final pose geometry consumes world foot targets",
          "[player][footik][p3b]") {
  football::player::PlayerBodyProfile body;
  body.height_scale = 1.0f;
  body.girth_scale = 1.0f;
  PlayerKinematicState state;
  PoseFrame pose;
  const BakedBodyPose raw = EvaluateBakedBodyPose(pose, state, 0.0f, body);
  PlayerFootIkTargets targets;
  targets.has_left = true;
  targets.left = raw.joints[BodyPart::left_ankle] + Vector3(0, 0, 0.05f);
  const BakedBodyPose posed =
      EvaluateBakedBodyPose(pose, state, 0.0f, body, &targets);
  CHECK((posed.joints[BodyPart::left_ankle] - targets.left).GetLength() <=
        1e-4f);
  CHECK((posed.joints[BodyPart::left_knee] - posed.joints[BodyPart::left_thigh])
            .GetLength() == Catch::Approx(
                                (raw.joints[BodyPart::left_knee] -
                                 raw.joints[BodyPart::left_thigh])
                                    .GetLength())
                                .margin(1e-4f));
}

TEST_CASE("P7-lite vertical authority overrides local bob exactly",
          "[player][vertical][p7lite]") {
  football::player::PlayerBodyProfile body;
  body.height_scale = 1.0f;
  body.girth_scale = 1.0f;
  PlayerKinematicState state;
  state.vertical_authoritative = true;
  state.vertical_offset = 0.75f;
  PoseFrame pose;
  pose.positions[BodyPart::player].coords[2] = 0.1f;
  const BakedBodyPose world = EvaluateBakedBodyPose(pose, state, 0.0f, body);
  CHECK(world.joints[BodyPart::player].coords[2] ==
        Catch::Approx(0.75f).margin(1e-6f));

  state.vertical_authoritative = false;
  const BakedBodyPose local = EvaluateBakedBodyPose(pose, state, 0.0f, body);
  CHECK(local.joints[BodyPart::player].coords[2] ==
        Catch::Approx(0.1f).margin(1e-6f));
}

// ---------------------------------------------------------------------------
// STRIDE-0/STRIDE-2: the support authority and the local-rotation write-back.
// ---------------------------------------------------------------------------

TEST_CASE("support slip and the plant lock are separate authorities",
          "[player][footik][stride0]") {
  PlayerFootPlantState plant;
  plant.left_planted = true;
  plant.left_anchor = Vector3(0.1f, 0.2f, 0.0f);
  // A planted foot that drifted 0.05 m in the plane, and a lifted but stationary
  // one: only horizontal motion counts as slide.
  const auto slip = MeasureFootPlantSlip(plant, Vector3(0.15f, 0.2f, 0.4f),
                                         Vector3(0.0f, 0.0f, 0.0f));
  CHECK(slip.left == Catch::Approx(0.05f).margin(1e-6f));
  CHECK(slip.right == Catch::Approx(0.0f));

  PlayerFootIkTargets targets;
  targets.has_left = true;
  targets.left = Vector3(0.15f, 0.2f, 0.0f);
  targets.has_right = true;
  targets.right = Vector3(-0.2f, 0.1f, 0.0f);
  const PlayerFootIkTargets locked = ApplyFootPlantLocks(plant, targets);
  CHECK(locked.has_left);
  CHECK(locked.left == plant.left_anchor);   // support overrides the gait
  CHECK(locked.has_right);                   // the swing foot keeps its target
  CHECK(locked.right == targets.right);
}

TEST_CASE("foot targets are written back into the local joint rotations",
          "[player][footik][stride2]") {
  football::player::PlayerBodyProfile body;
  body.height_scale = 1.0f;
  body.girth_scale = 1.0f;
  PlayerKinematicState state;
  const PoseFrame rest;
  const BakedBodyPose raw = EvaluateBakedBodyPose(rest, state, 0.0f, body);

  PlayerFootIkTargets targets;
  targets.has_left = true;
  // Move the left ankle 0.15 m forward (-Y) and 0.05 m up.
  targets.left = raw.joints[BodyPart::left_ankle] + Vector3(0, -0.15f, 0.05f);
  const BakedPoseFootSolve solved =
      ApplyWorldFootTargetsToPose(rest, state, 0.0f, body, targets);
  REQUIRE(solved.left_applied);
  CHECK(solved.left_residual <= 1e-4f);

  const BakedBodyPose settled =
      EvaluateBakedBodyPose(solved.pose, state, 0.0f, body);
  // The skeleton itself reaches the target: geometry, colliders and touch all
  // read this pose instead of re-solving a different one.
  CHECK((settled.joints[BodyPart::left_ankle] - targets.left).GetLength() <=
        1e-4f);
  // Bone lengths are untouched.
  CHECK((settled.joints[BodyPart::left_knee] -
         settled.joints[BodyPart::left_thigh])
            .GetLength() ==
        Catch::Approx((raw.joints[BodyPart::left_knee] -
                       raw.joints[BodyPart::left_thigh])
                          .GetLength())
            .margin(1e-5f));
  CHECK((settled.joints[BodyPart::left_ankle] -
         settled.joints[BodyPart::left_knee])
            .GetLength() ==
        Catch::Approx((raw.joints[BodyPart::left_ankle] -
                       raw.joints[BodyPart::left_knee])
                          .GetLength())
            .margin(1e-5f));
  // The boot keeps its world orientation while the shin swings.
  const auto before = raw.orientations[BodyPart::left_ankle];
  const auto after = settled.orientations[BodyPart::left_ankle];
  CHECK(std::fabs(before.GetDotProduct(after)) ==
        Catch::Approx(1.0f).margin(1e-5f));
  // The untouched leg is bit-identical.
  CHECK(settled.joints[BodyPart::right_ankle].coords[0] ==
        raw.joints[BodyPart::right_ankle].coords[0]);
  CHECK(settled.joints[BodyPart::right_ankle].coords[1] ==
        raw.joints[BodyPart::right_ankle].coords[1]);
}

TEST_CASE("an unreachable foot target is clamped and reported",
          "[player][footik][stride2]") {
  football::player::PlayerBodyProfile body;
  body.height_scale = 1.0f;
  body.girth_scale = 1.0f;
  PlayerKinematicState state;
  const PoseFrame rest;
  const BakedBodyPose raw = EvaluateBakedBodyPose(rest, state, 0.0f, body);
  PlayerFootIkTargets targets;
  targets.has_left = true;
  targets.left = raw.joints[BodyPart::left_ankle] + Vector3(0, -2.0f, 0.0f);
  const BakedPoseFootSolve solved =
      ApplyWorldFootTargetsToPose(rest, state, 0.0f, body, targets);
  CHECK(solved.left_residual > 0.05f);
  const BakedBodyPose settled =
      EvaluateBakedBodyPose(solved.pose, state, 0.0f, body);
  // Still within reach of the hip.
  CHECK((settled.joints[BodyPart::left_ankle] -
         settled.joints[BodyPart::left_thigh])
            .GetLength() <= 0.87f);
}

TEST_CASE("the pose write-back is idempotent under re-evaluation",
          "[player][footik][stride2]") {
  football::player::PlayerBodyProfile body;
  body.height_scale = 1.0f;
  body.girth_scale = 1.0f;
  PlayerKinematicState state;
  const PoseFrame rest;
  const BakedBodyPose raw = EvaluateBakedBodyPose(rest, state, 0.0f, body);
  PlayerFootIkTargets targets;
  targets.has_left = true;
  targets.has_right = true;
  targets.left = raw.joints[BodyPart::left_ankle] + Vector3(0, -0.10f, 0.02f);
  targets.right = raw.joints[BodyPart::right_ankle] + Vector3(0, 0.10f, 0.0f);
  const BakedPoseFootSolve once =
      ApplyWorldFootTargetsToPose(rest, state, 0.0f, body, targets);
  const BakedPoseFootSolve twice =
      ApplyWorldFootTargetsToPose(once.pose, state, 0.0f, body, targets);
  // Re-applying the same targets does not move the feet again; that is what lets
  // the collider and the renderer share one pose.
  CHECK((twice.pose.orientations[BodyPart::left_thigh].elements[0] -
         once.pose.orientations[BodyPart::left_thigh].elements[0]) ==
        Catch::Approx(0.0f).margin(1e-4f));
  CHECK(twice.left_residual <=
        once.left_residual + 1e-4f);
}
