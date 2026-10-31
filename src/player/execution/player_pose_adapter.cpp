#include "football/player/execution/player_pose_adapter.hpp"

#include <algorithm>
#include <cmath>

using football::math::Vector3;

namespace {

constexpr float kDirectionEpsilon = 1e-6f;
constexpr float kWarpEpsilon = 1e-4f;

float NormalizeAngle(float angle) {
  while (angle > football::math::pi) angle -= 2.0f * football::math::pi;
  while (angle < -football::math::pi) angle += 2.0f * football::math::pi;
  return angle;
}

// Fore-aft axis of the body frame, in world XY. No lateral helper is needed:
// everything that is not fore-aft is passed through untouched, so a stride
// change can never widen the stance.
Vector3 FacingAxis(const Vector3& facing) {
  return facing.Get2D().GetNormalized(Vector3(0, -1, 0));
}

}  // namespace

PlayerPoseWarpResult PlayerPoseAdapter::Warp(const PlayerPoseWarpInput& input) {
  PlayerPoseWarpResult result;
  result.pose = input.reference_pose;
  result.base_angle = input.reference_base_angle;

  const Vector3 reference = input.reference_delta.Get2D();
  const Vector3 achieved = input.achieved_delta.Get2D();
  const float reference_length = reference.GetLength();
  const float achieved_length = achieved.GetLength();
  if (reference_length <= kDirectionEpsilon ||
      achieved_length <= kDirectionEpsilon) {
    return result;
  }

  const float cross = reference.coords[0] * achieved.coords[1] -
                      reference.coords[1] * achieved.coords[0];
  const float dot = reference.coords[0] * achieved.coords[0] +
                    reference.coords[1] * achieved.coords[1];
  const float yaw_correction = NormalizeAngle(std::atan2(cross, dot));
  const float bob_ratio = std::clamp(achieved_length / reference_length,
                                     0.5f, 1.5f);

  if (std::fabs(yaw_correction) <= kWarpEpsilon &&
      std::fabs(bob_ratio - 1.0f) <= kWarpEpsilon) {
    return result;
  }

  result.base_angle =
      NormalizeAngle(input.reference_base_angle + yaw_correction);
  result.yaw_correction = yaw_correction;
  // Baked root Z is pelvis bob, not world height. Warping it keeps the visual
  // cadence proportional to the distance the root actually achieved.
  result.pose.positions[BodyPart::player].coords[2] *= bob_ratio;
  result.pose_warped = true;
  return result;
}

PlayerStrideAdaptResult PlayerPoseAdapter::AdaptStride(
    const PlayerStrideAdaptInput& input) {
  PlayerStrideAdaptResult result;
  const Vector3 forward_axis = FacingAxis(input.facing);
  const float scale =
      std::isfinite(input.stride_scale) && input.stride_scale > 0.0f
          ? input.stride_scale
          : 1.0f;
  static_cast<void>(input.gait_phase);  // carried for diagnostics only

  const auto adapt_leg = [&](const PlayerStrideAdaptLeg& leg, bool& has_target,
                             Vector3& target, bool& adapted) {
    if (!leg.valid) return;
    if (!std::isfinite(leg.hip.coords[0]) ||
        !std::isfinite(leg.ankle.coords[0])) {
      return;
    }
    // Only the hip-relative fore-aft excursion changes. Bone lengths are not
    // touched: the two-bone solve downstream reconstructs the knee.
    const Vector3 offset = leg.ankle - leg.hip;
    const float fore_aft = offset.GetDotProduct(forward_axis);
    const float adapted_fore_aft = fore_aft * scale;
    const Vector3 residual =
        offset - forward_axis * fore_aft;  // lateral + vertical, untouched
    has_target = true;
    target = leg.hip + forward_axis * adapted_fore_aft + residual;
    adapted = true;
    result.max_fore_aft_correction =
        std::max(result.max_fore_aft_correction,
                 std::fabs(adapted_fore_aft - fore_aft));
  };

  adapt_leg(input.left, result.targets.has_left, result.targets.left,
            result.left_adapted);
  adapt_leg(input.right, result.targets.has_right, result.targets.right,
            result.right_adapted);
  return result;
}
