// P3a — pure root/pose warp contract. No Animation playback, no contact, no
// runtime: reference pose + deltas in, warped pose/base angle out.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/player/execution/player_pose_adapter.hpp"

using football::math::Vector3;

namespace {

PlayerPoseWarpInput Input(const Vector3& reference_delta,
                          const Vector3& achieved_delta,
                          float base_angle = 0.0f) {
  PlayerPoseWarpInput input;
  input.reference_delta = reference_delta;
  input.achieved_delta = achieved_delta;
  input.reference_base_angle = base_angle;
  input.reference_pose.positions[BodyPart::player] = Vector3(0, 0, 0.1f);
  input.reference_pose.orientations[BodyPart::body].SetAngleAxis(
      0.2f, Vector3(0, 0, 1));
  return input;
}

}  // namespace

TEST_CASE("pose adapter identity preserves the reference pose exactly",
          "[player][pose][adapter][p3]") {
  const auto input = Input(Vector3(0, -1, 0), Vector3(0, -1, 0), 0.4f);
  const auto result = PlayerPoseAdapter::Warp(input);
  CHECK_FALSE(result.pose_warped);
  CHECK_FALSE(result.foot_lock_applied);
  CHECK(result.base_angle == Catch::Approx(0.4f));
  CHECK(result.pose.positions[BodyPart::player].coords[2] ==
        Catch::Approx(0.1f));
}

TEST_CASE("pose adapter warps base yaw toward the achieved direction",
          "[player][pose][adapter][p3]") {
  const auto result = PlayerPoseAdapter::Warp(
      Input(Vector3(0, -1, 0), Vector3(1, 0, 0), 0.0f));
  CHECK(result.pose_warped);
  CHECK(result.yaw_correction ==
        Catch::Approx(football::math::pi * 0.5f).margin(1e-5f));
  CHECK(result.base_angle ==
        Catch::Approx(football::math::pi * 0.5f).margin(1e-5f));
}

TEST_CASE("pose adapter scales pelvis bob with achieved distance",
          "[player][pose][adapter][p3]") {
  const auto result = PlayerPoseAdapter::Warp(
      Input(Vector3(0, -1, 0), Vector3(0, -0.5f, 0), 0.0f));
  CHECK(result.pose_warped);
  CHECK(result.yaw_correction == Catch::Approx(0.0f).margin(1e-6f));
  CHECK(result.pose.positions[BodyPart::player].coords[2] ==
        Catch::Approx(0.05f));
}

TEST_CASE("pose adapter leaves zero-direction references untouched",
          "[player][pose][adapter][p3]") {
  const auto result = PlayerPoseAdapter::Warp(
      Input(Vector3(0), Vector3(1, 0, 0), 0.3f));
  CHECK_FALSE(result.pose_warped);
  CHECK(result.base_angle == Catch::Approx(0.3f));
  CHECK(result.pose.positions[BodyPart::player].coords[2] ==
        Catch::Approx(0.1f));
}

// ---------------------------------------------------------------------------
// STRIDE-1: the stride ratio shortens the gait's fore-aft foot excursion. It is
// a pure transform of the world skeleton; support and bone lengths are somebody
// else's authority.
// ---------------------------------------------------------------------------
namespace {

PlayerStrideAdaptInput StrideInput(float scale) {
  PlayerStrideAdaptInput input;
  input.left.valid = true;
  input.left.hip = Vector3(0.09f, 0.0f, 0.9f);
  input.left.ankle = Vector3(0.09f, -0.3f, 0.05f);   // 0.30 m in front
  input.right.valid = true;
  input.right.hip = Vector3(-0.09f, 0.0f, 0.9f);
  input.right.ankle = Vector3(-0.09f, 0.2f, 0.05f);  // 0.20 m behind
  input.facing = Vector3(0, -1, 0);                  // forward is -Y
  input.stride_scale = scale;
  return input;
}

}  // namespace

TEST_CASE("stride scale 1 reproduces the gait's own ankle targets",
          "[player][stride][stride1]") {
  const auto input = StrideInput(1.0f);
  const auto result = PlayerPoseAdapter::AdaptStride(input);
  CHECK(result.targets.has_left);
  CHECK(result.targets.has_right);
  CHECK(result.targets.left.coords[1] ==
        Catch::Approx(input.left.ankle.coords[1]).margin(1e-6f));
  CHECK(result.targets.right.coords[1] ==
        Catch::Approx(input.right.ankle.coords[1]).margin(1e-6f));
  CHECK(result.max_fore_aft_correction == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("stride scale shortens only the fore-aft excursion",
          "[player][stride][stride1]") {
  const auto input = StrideInput(0.5f);
  const auto result = PlayerPoseAdapter::AdaptStride(input);
  // The front foot was 0.30 m ahead and is now 0.15 m ahead; the trailing foot
  // mirrors it. Lateral and vertical components are untouched.
  CHECK((result.targets.left.coords[1] - input.left.hip.coords[1]) ==
        Catch::Approx(-0.15f).margin(1e-5f));
  CHECK((result.targets.right.coords[1] - input.right.hip.coords[1]) ==
        Catch::Approx(0.10f).margin(1e-5f));
  CHECK(result.targets.left.coords[0] ==
        Catch::Approx(input.left.ankle.coords[0]));
  CHECK(result.targets.left.coords[2] ==
        Catch::Approx(input.left.ankle.coords[2]));
  CHECK(result.max_fore_aft_correction == Catch::Approx(0.15f).margin(1e-5f));
}

TEST_CASE("stride adaptation follows the body facing, not the world axes",
          "[player][stride][stride1]") {
  auto input = StrideInput(0.5f);
  // Facing +X: the same foot is 0.30 m ahead along +X now.
  input.facing = Vector3(1, 0, 0);
  input.left.hip = Vector3(0.0f, 0.09f, 0.9f);
  input.left.ankle = Vector3(0.3f, 0.09f, 0.05f);
  input.right.hip = Vector3(0.0f, -0.09f, 0.9f);
  input.right.ankle = Vector3(-0.2f, -0.09f, 0.05f);
  const auto result = PlayerPoseAdapter::AdaptStride(input);
  CHECK((result.targets.left.coords[0] - input.left.hip.coords[0]) ==
        Catch::Approx(0.15f).margin(1e-5f));
  CHECK(result.targets.left.coords[1] ==
        Catch::Approx(input.left.ankle.coords[1]));
}

TEST_CASE("stride adaptation ignores degenerate legs and bad scales",
          "[player][stride][stride1]") {
  PlayerStrideAdaptInput input;
  const auto empty = PlayerPoseAdapter::AdaptStride(input);
  CHECK_FALSE(empty.targets.has_left);
  CHECK_FALSE(empty.targets.has_right);

  auto scaled = StrideInput(-1.0f);  // rejected: keeps the gait as-is
  const auto result = PlayerPoseAdapter::AdaptStride(scaled);
  CHECK(result.targets.left.coords[1] ==
        Catch::Approx(scaled.left.ankle.coords[1]).margin(1e-6f));
}
