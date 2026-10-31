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
