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

// ---------------------------------------------------------------------------
// M3: bounded realization. The pose a body can reach in one tick, not the pose
// an animation can key in one tick.
// ---------------------------------------------------------------------------
namespace {

PoseFrame PoseWithBodyYaw(float yaw_rad) {
  PoseFrame pose;
  pose.orientations[BodyPart::body].SetAngleAxis(yaw_rad, Vector3(0, 0, 1));
  pose.orientations[BodyPart::left_ankle].SetAngleAxis(yaw_rad, Vector3(1, 0, 0));
  pose.positions[BodyPart::player] = Vector3(0, 0, 0.1f);
  return pose;
}

PlayerPoseRealizeInput RealizeInput(const PoseFrame& previous,
                                    const PoseFrame& target) {
  PlayerPoseRealizeInput input;
  input.previous_pose = previous;
  input.target_pose = target;
  input.body.height_scale = 1.0f;
  input.body.girth_scale = 1.0f;
  return input;
}

float QuatAngle(const football::math::Quaternion& a,
                const football::math::Quaternion& b) {
  auto x = a;
  auto y = b;
  const float dot = x.MakeSameNeighborhood(y);
  return 2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f));
}

}  // namespace

TEST_CASE("realization is the identity for an unmoved pose",
          "[player][pose][realize][m3]") {
  const PoseFrame pose = PoseWithBodyYaw(0.3f);
  const auto result = PlayerPoseAdapter::Realize(RealizeInput(pose, pose));
  CHECK_FALSE(result.limited);
  CHECK(result.pose.positions[BodyPart::player].coords[2] ==
        Catch::Approx(0.1f));
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    CHECK(result.pose.orientations[joint].elements[3] ==
          Catch::Approx(pose.orientations[joint].elements[3]).margin(1e-6f));
  }
}

TEST_CASE("realization bounds how far a joint may turn in one tick",
          "[player][pose][realize][m3]") {
  const PoseFrame previous;
  // A half turn in one tick is far beyond any joint budget.
  const PoseFrame target = PoseWithBodyYaw(football::math::pi);
  const auto result = PlayerPoseAdapter::Realize(RealizeInput(previous, target));
  CHECK(result.limited);
  const float turned = QuatAngle(previous.orientations[BodyPart::body],
                                 result.pose.orientations[BodyPart::body]);
  // 7.5*pi rad/s for one tick.
  CHECK(turned <= 7.5f * football::math::pi * 0.01f + 1e-4f);
  CHECK(turned > 0.0f);
}

TEST_CASE("realization converges onto a target over successive ticks",
          "[player][pose][realize][m3]") {
  const PoseFrame target = PoseWithBodyYaw(0.9f);
  PoseFrame current;
  float previous_angle = 0.9f;
  for (int tick = 0; tick < 12; ++tick) {
    const auto result = PlayerPoseAdapter::Realize(RealizeInput(current, target));
    current = result.pose;
    const float angle = QuatAngle(current.orientations[BodyPart::body],
                                  target.orientations[BodyPart::body]);
    // Monotone approach, never overshooting the target.
    CHECK(angle <= previous_angle + 1e-5f);
    previous_angle = angle;
  }
  CHECK(previous_angle < 1e-3f);
}

TEST_CASE("entering a clip damps the realization budget",
          "[player][pose][realize][m3]") {
  const PoseFrame previous;
  const PoseFrame target = PoseWithBodyYaw(0.5f);
  auto limited = RealizeInput(previous, target);
  auto entering = RealizeInput(previous, target);
  entering.entry_weight = 1.0f;
  const auto tight = PlayerPoseAdapter::Realize(limited);
  const auto loose = PlayerPoseAdapter::Realize(entering);
  const float tight_turn = QuatAngle(previous.orientations[BodyPart::body],
                                     tight.pose.orientations[BodyPart::body]);
  const float loose_turn = QuatAngle(previous.orientations[BodyPart::body],
                                     loose.pose.orientations[BodyPart::body]);
  // Entering a clip is followed more cautiously than a running one.
  CHECK(loose_turn < tight_turn);
}

TEST_CASE("the base yaw is not touched by realization",
          "[player][pose][realize][m3]") {
  // Realization works on local joint rotations only: the caller applies the
  // (transition-smoothed) base yaw when it builds geometry, so animation
  // matching still compares local poses.
  const PoseFrame target = PoseWithBodyYaw(0.2f);
  const auto result = PlayerPoseAdapter::Realize(RealizeInput(PoseFrame{}, target));
  // A pose whose only orientation is the body is still expressed locally.
  CHECK(result.pose.orientations[BodyPart::player].elements[3] ==
        Catch::Approx(1.0f));
}

TEST_CASE("warp bounds the body yaw it corrects per tick",
          "[player][pose][warp][m3]") {
  auto input = Input(Vector3(0, -1, 0), Vector3(1, 0, 0), 0.0f);
  input.max_yaw_correction = 0.12f;
  const auto result = PlayerPoseAdapter::Warp(input);
  CHECK(result.yaw_clamped);
  CHECK(result.yaw_correction == Catch::Approx(0.12f).margin(1e-5f));
  CHECK(result.base_angle == Catch::Approx(0.12f).margin(1e-5f));

  // Inside the budget the correction is applied whole and not flagged.
  auto small = Input(Vector3(0, -1, 0), Vector3(0.05f, -1, 0), 0.0f);
  small.max_yaw_correction = 0.5f;
  const auto unbounded = PlayerPoseAdapter::Warp(small);
  CHECK_FALSE(unbounded.yaw_clamped);
  CHECK(std::fabs(unbounded.yaw_correction) < 0.5f);
}
