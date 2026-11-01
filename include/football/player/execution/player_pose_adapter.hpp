#ifndef FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP
#define FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP

#include <cmath>
#include <limits>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"

// P3a — pure root/pose warp foundation.
//
// The reference pose is sampled by Animation; the committed root is owned by
// the root-driven or compatibility motion path. This adapter is the one place
// allowed to make the final pose agree with the *achieved* root before contact
// geometry is built:
//
//   - yaw: rotate the body/base angle by the difference between the reference
//     displacement direction and the achieved displacement direction;
//   - vertical bob: scale the pelvis bob by the achieved/reference distance
//     ratio so a slowed stride does not keep a sprint bob.
//
// Foot planting is deliberately NOT claimed here. Runtime foot IK was removed;
// the support foot is kept consistent by the IK-free source gate and the
// sampled root/pose contract, not by a plant solve.
struct PlayerPoseWarpInput {
  PoseFrame reference_pose;
  float reference_base_angle = 0.0f;
  football::math::Vector3 reference_delta = football::math::Vector3(0);
  football::math::Vector3 achieved_delta = football::math::Vector3(0);
  football::player::PlayerBodyProfile body;
  // A yaw correction is a rendering/pose correction, not a turn: without a bound
  // a reference/achieved direction mismatch would snap the whole body in one
  // tick. 0 disables the bound (the pure transform), a production caller passes
  // a per-tick rate.
  float max_yaw_correction = 0.0f;
  // Per-player style: scales the pelvis bob before the achieved/reference ratio
  // is applied. 1.0 = as authored.
  float pelvis_bob_scale = 1.0f;
};

// Default per-tick yaw budget for the warp (12 rad/s over a 10 ms tick, ~7 deg).
inline constexpr float kPlayerPoseWarpMaxYawPerTick = 0.12f;

struct PlayerPoseWarpResult {
  PoseFrame pose;
  float base_angle = 0.0f;
  float yaw_correction = 0.0f;
  bool yaw_clamped = false;
  bool pose_warped = false;
};

// Reach-aware stride ceiling, used by the per-player animation configuration to
// bound a pre-baked stride variant. This is not a runtime leg solve.
//
// The bound is geometric: with the hip-relative ankle offset split into a
// fore-aft part `f` and the rest `r`, a scaled target sits at
// `(S*f)^2 + |r|^2 <= reach^2`.
//
//   S_max = sqrt(max(0, reach^2 - |r|^2)) / |f|
inline constexpr float kStrideReachSafety = 0.98f;

inline float LegStrideReachLimit(const football::math::Vector3& hip,
                                 const football::math::Vector3& ankle,
                                 const football::math::Vector3& facing,
                                 float reach) {
  if (reach <= 0.0f) return std::numeric_limits<float>::infinity();
  const football::math::Vector3 forward =
      facing.Get2D().GetNormalized(football::math::Vector3(0, -1, 0));
  const football::math::Vector3 offset = ankle - hip;
  const float fore_aft = offset.GetDotProduct(forward);
  if (std::fabs(fore_aft) <= 1e-4f) {
    return std::numeric_limits<float>::infinity();
  }
  const football::math::Vector3 rest = offset - forward * fore_aft;
  const float safe_reach = reach * kStrideReachSafety;
  const float budget =
      safe_reach * safe_reach - rest.GetLength() * rest.GetLength();
  if (budget <= 0.0f) return 0.0f;
  return std::sqrt(budget) / std::fabs(fore_aft);
}

// M3 — bounded realization of the Final Pose.
//
// The Animation layer may produce a joint rotation that a real body cannot
// reach in one tick. This stage rate-limits every joint of the *target* pose
// against the previous Final Pose, so the skeleton that physics, contact and
// the renderer receive never snaps.
//
// `entry_weight` is the clip-entry damping: 1 on the first frames of a clip
// (only 30% of the joint budget is available), 0 once the clip is running (the
// full budget). It is the caller's clip-time fact, derived from the action
// cursor, which is the clip-time authority for both action and movement clips.
struct PlayerPoseRealizeInput {
  PoseFrame previous_pose;
  PoseFrame target_pose;
  football::player::PlayerBodyProfile body;
  float entry_weight = 0.0f;
  // An exiting special state may drop the pelvis faster than a normal stride.
  bool outgoing_special_state = false;
};

struct PlayerPoseRealizeResult {
  PoseFrame pose;
  bool limited = false;
};

class PlayerPoseAdapter final {
 public:
  // Pure and deterministic. Identity inputs return the reference pose exactly.
  static PlayerPoseWarpResult Warp(const PlayerPoseWarpInput& input);

  // M3: the one bounded-realization stage of the Final Pose channel. The base
  // yaw is *not* touched: it is blended by the transition and applied later, so
  // animation matching still compares local joint rotations.
  static PlayerPoseRealizeResult Realize(const PlayerPoseRealizeInput& input);
};

#endif  // FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP
