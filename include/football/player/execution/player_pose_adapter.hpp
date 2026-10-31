#ifndef FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP
#define FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"

// P3a — pure root/pose warp foundation.
//
// The reference pose is sampled by Animation; the committed root is owned by
// PlayerMotionAdapter. This adapter is the one place allowed to make the final
// pose agree with the *achieved* root before contact geometry is built:
//
//   - yaw: rotate the body/base angle by the difference between the reference
//     displacement direction and the achieved displacement direction;
//   - vertical bob: scale the pelvis bob by the achieved/reference distance
//     ratio so a slowed stride does not keep a sprint bob.
//
// Foot planting is deliberately NOT claimed here. It needs the P1 foot-plant
// events and a two-bone IK solve, neither of which is in the current bake. The
// result therefore reports `foot_lock_applied == false`; callers must not treat
// this as a complete foot-IK implementation.
struct PlayerPoseWarpInput {
  PoseFrame reference_pose;
  float reference_base_angle = 0.0f;
  football::math::Vector3 reference_delta = football::math::Vector3(0);
  football::math::Vector3 achieved_delta = football::math::Vector3(0);
  football::player::PlayerBodyProfile body;
};

struct PlayerPoseWarpResult {
  PoseFrame pose;
  float base_angle = 0.0f;
  float yaw_correction = 0.0f;
  bool pose_warped = false;
  bool foot_lock_applied = false;
};

class PlayerPoseAdapter final {
 public:
  // Pure and deterministic. Identity inputs return the reference pose exactly.
  static PlayerPoseWarpResult Warp(const PlayerPoseWarpInput& input);
};

#endif  // FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP
