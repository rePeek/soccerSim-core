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
