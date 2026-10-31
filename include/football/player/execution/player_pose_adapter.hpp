#ifndef FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP
#define FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/player/execution/player_foot_ik.hpp"
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
// Foot planting is deliberately NOT claimed here: it needs the solved ankle
// targets the executor owns (see PlayerStrideAdaptInput / ApplyStride).
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
  bool foot_lock_applied = false;
};

// STRIDE-1 — one leg as the *world skeleton* currently has it. The executor
// supplies these joints because it owns forward kinematics; the adapter stays a
// pure transform and never reads a clip, a Player or a world position.
struct PlayerStrideAdaptLeg {
  bool valid = false;
  football::math::Vector3 hip = football::math::Vector3(0);
  football::math::Vector3 ankle = football::math::Vector3(0);
};

struct PlayerStrideAdaptInput {
  // The gait the animation produced, before any support authority applies.
  PlayerStrideAdaptLeg left;
  PlayerStrideAdaptLeg right;
  // World facing of the body frame the animation was sampled in.
  football::math::Vector3 facing = football::math::Vector3(0, -1, 0);
  // 1.0 keeps the clip's own stride length.
  float stride_scale = 1.0f;
  // 0..1 within the clip cycle; carried so a caller can reason about stride
  // diagnostics without re-deriving it.
  float gait_phase = 0.0f;
};

struct PlayerStrideAdaptResult {
  // World-space ankle targets for the settled final pose, one per valid leg.
  // A support foot is *not* handled here: the plant owns it (see
  // ApplyFootPlantLocks), so the stride ratio can never stretch or drag it.
  PlayerFootIkTargets targets;
  bool left_adapted = false;
  bool right_adapted = false;
  // Largest fore-aft change applied to a leg (metres, unsigned).
  float max_fore_aft_correction = 0.0f;
};

// M3 — bounded realization of the Final Pose.
//
// The Animation layer may produce a joint rotation that a real body cannot
// reach in one tick. This stage rate-limits every joint of the *target* pose
// against the previous Final Pose, so the skeleton that physics, contact and
// the renderer receive never snaps. It is deliberately the last animation-side
// step before the stride/plant solve: limiting first and solving the feet
// afterwards is what keeps the support foot locked.
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

  // STRIDE-1: shorten/lengthen the gait's fore-aft foot excursion about the hip
  // while both bone lengths stay untouched; the knee is reconstructed by the
  // two-bone solve downstream.
  //
  // The caller pairs `stride_scale` with the playback rate that divides by the
  // same value (AdvanceLocomotionPlayback), which keeps the stance foot in step
  // with the root while the stride shortens. Support feet are then released
  // from the gait by PlayerFootIk's ApplyFootPlantLocks().
  static PlayerStrideAdaptResult AdaptStride(const PlayerStrideAdaptInput& input);

  // M3: the one bounded-realization stage of the Final Pose channel. The base
  // yaw is *not* touched: it is blended by the transition and applied later, so
  // animation matching still compares local joint rotations.
  static PlayerPoseRealizeResult Realize(const PlayerPoseRealizeInput& input);
};

#endif  // FOOTBALL_SIM_PLAYER_POSE_ADAPTER_HPP
