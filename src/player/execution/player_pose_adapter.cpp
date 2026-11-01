#include "football/player/execution/player_pose_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
  float yaw_correction = NormalizeAngle(std::atan2(cross, dot));
  if (input.max_yaw_correction > 0.0f) {
    const float limit = input.max_yaw_correction;
    if (yaw_correction > limit) {
      yaw_correction = limit;
      result.yaw_clamped = true;
    } else if (yaw_correction < -limit) {
      yaw_correction = -limit;
      result.yaw_clamped = true;
    }
  }
  const float bob_ratio = std::clamp(achieved_length / reference_length,
                                     0.5f, 1.5f);
  // The player's own vertical style is a pose fact, not a motion correction: it
  // must apply even when the reference and achieved motion agree exactly.
  const float style_scale =
      std::isfinite(input.pelvis_bob_scale) && input.pelvis_bob_scale > 0.0f
          ? input.pelvis_bob_scale
          : 1.0f;

  if (std::fabs(yaw_correction) <= kWarpEpsilon &&
      std::fabs(bob_ratio - 1.0f) <= kWarpEpsilon &&
      std::fabs(style_scale - 1.0f) <= kWarpEpsilon) {
    return result;
  }

  result.base_angle =
      NormalizeAngle(input.reference_base_angle + yaw_correction);
  result.yaw_correction = yaw_correction;
  // Baked root Z is pelvis bob, not world height. Warping it keeps the visual
  // cadence proportional to the distance the root actually achieved; the
  // player's style scale decides how pronounced his bob is in the first place.
  result.pose.positions[BodyPart::player].coords[2] *=
      bob_ratio * style_scale;
  result.pose_warped = true;
  return result;
}

namespace {

// Per-joint rotation budget of the bounded realization. The values are the
// shipped animation-realization constants: a joint may turn this fast before the
// Player refuses to follow the clip. Knees and ankles are allowed more, because
// a stride genuinely rotates them hardest.
float JointRateLimit(BodyPart joint) {
  float rate = 7.5f * football::math::pi;
  if (joint == BodyPart::left_elbow || joint == BodyPart::right_elbow ||
      joint == BodyPart::left_knee || joint == BodyPart::right_knee) {
    rate *= 1.2f;
  }
  if (joint == BodyPart::left_ankle || joint == BodyPart::right_ankle) {
    rate *= 1.6f;
  }
  return rate;
}

}  // namespace

PlayerPoseRealizeResult PlayerPoseAdapter::Realize(
    const PlayerPoseRealizeInput& input) {
  PlayerPoseRealizeResult result;
  result.pose = input.target_pose;
  constexpr float kTickSeconds = 0.01f;
  const float begin = std::clamp(input.entry_weight, 0.0f, 1.0f);

  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    // The root is the Player's own transform, never an animation joint.
    if (joint == BodyPart::player) continue;
    const auto part = static_cast<BodyPart>(joint);
    Quaternion desired = input.target_pose.orientations[joint];
    const Quaternion& current = input.previous_pose.orientations[joint];
    desired.MakeSameNeighborhood(current);
    const float dot = desired.MakeSameNeighborhood(current);
    const float angle = 2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f));
    // Entering a clip damps the follow budget; a running body gets all of it.
    const float allowed = (0.3f + 0.7f * (1.0f - begin)) * JointRateLimit(part) *
                          kTickSeconds;
    if (angle > allowed) {
      const Quaternion delta = current.GetRotationTo(desired).GetNormalized();
      desired =
          (delta.GetRotationMultipliedBy(allowed / angle) * current).GetNormalized();
      result.limited = true;
    }
    result.pose.orientations[joint] = desired;
  }

  // Pelvis bob: blend toward the previous height and bound its rate, so a clip
  // entry cannot drop the body through the floor in one tick.
  if (begin > 0.01f) {
    const float scale = input.body.height_scale;
    const float old_z = input.previous_pose.positions[BodyPart::player].coords[2];
    float z = input.target_pose.positions[BodyPart::player].coords[2];
    if (std::isfinite(scale) && scale > 0.0f) {
      const float old_world = old_z * scale;
      float world = z * scale;
      world = world * (1.0f - begin * 0.5f) + old_world * (begin * 0.5f);
      const float allowed_bob =
          (input.outgoing_special_state ? 6.0f : 2.8f) * kTickSeconds;
      const float limited =
          old_world + std::clamp(world - old_world, -allowed_bob, allowed_bob);
      world = limited * begin + world * (1.0f - begin);
      z = world / scale;
    }
    result.pose.positions[BodyPart::player].coords[2] = z;
  }
  return result;
}
