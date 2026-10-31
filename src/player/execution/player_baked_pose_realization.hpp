#ifndef FOOTBALL_PLAYER_BAKED_POSE_REALIZATION_HPP
#define FOOTBALL_PLAYER_BAKED_POSE_REALIZATION_HPP
#include "football/foundation/time/tick_boundary.hpp"
#include "football/player/execution/player_pose_adapter.hpp"
#include "player/execution/player_baked_pose_sample.hpp"

// FORECAST-ONLY animation realization, NOT a collision filter.
//
// The committed pose no longer passes through here: the Player applies the same
// bounded realization inside its Final Pose channel
// (`PlayerPoseAdapter::Realize`) and Runtime consumes that pose as-is. Only the
// speculative body forecast still uses this helper, because a forecast is not a
// commitment and must not be written back into the Player.
//
// `WorldBakedPose` folds the base yaw into the body joint, which is the shape
// the physics tape works in. `RealizeBakedPose` then rate-limits against the
// previous realized pose; it never re-solves a pose the Player already settled.

inline PlayerBakedPoseSample WorldBakedPose(PlayerBakedPoseSample target) {
  football::math::Quaternion base;
  base.SetAngleAxis(target.base_angle, {0, 0, 1});
  target.pose.orientations[BodyPart::body] =
      (base * target.pose.orientations[BodyPart::body]).GetNormalized();
  target.base_angle = 0;
  return target;
}

inline PlayerBakedPoseSample RealizeBakedPose(
    const PlayerBakedPoseSample& previous, const PlayerBakedPoseSample& input,
    bool outgoing_special_state = false) {
  auto next = WorldBakedPose(input);
  const auto old = WorldBakedPose(previous);
  if (!(next.body_profile.height_scale > 0) ||
      !std::isfinite(next.body_profile.height_scale) ||
      next.body_profile.height_scale != old.body_profile.height_scale) {
    throw std::invalid_argument(
        "baked realization requires a fixed positive actor height scale");
  }
  PlayerPoseRealizeInput realize;
  realize.previous_pose = old.pose;
  realize.target_pose = next.pose;
  realize.body = next.body_profile;
  // Entry smoothing is the bounded rotation-rate ramp, never a slerp toward the
  // previous pose: that would accumulate lag and release it as a surface-speed
  // spike when the entry window closes.
  realize.entry_weight = std::sqrt(football::math::curve(
      1.0f - football::math::NormalizedClamp(float(input.sampled_frame), 0, 8),
      1));
  realize.outgoing_special_state = outgoing_special_state;
  next.pose = PlayerPoseAdapter::Realize(realize).pose;
  return next;
}

#endif
