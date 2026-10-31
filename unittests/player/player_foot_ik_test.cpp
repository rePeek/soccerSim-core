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
  const Vector3 anchor = state.left_anchor;
  CHECK(anchor == input.left_ankle);

  input.left_ankle = Vector3(0.2f, 0.3f, 0.1f);
  state = PlayerFootIk::Update(state, input);
  CHECK(state.left_anchor == anchor);

  input.left_support = false;
  state = PlayerFootIk::Update(state, input);
  CHECK_FALSE(state.left_planted);

  input.left_support = true;
  input.left_ankle = Vector3(0.0f, 0.0f, 0.5f);
  state = PlayerFootIk::Update(state, input);
  CHECK_FALSE(state.left_planted);
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
