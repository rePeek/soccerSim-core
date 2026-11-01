#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "football/animation/types.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

using namespace football::math;

// Number of animated body parts. Must match the legacy BodyPart::body_part_max
// so that a baked pose can be compared index-for-index against
// Animation::GetKeyFrame(BodyPart, ...).
constexpr size_t kBodyPartCount = 14;

// Per-frame joint pose, one entry per body part (indices follow the legacy
// BodyPart enum order: middle..player).
struct PoseFrame {
  std::array<Quaternion, kBodyPartCount> orientations;
  std::array<Vector3, kBodyPartCount> positions;
};

// Typed, baked animation metadata. The runtime never reads string variables.
struct FootballAnimationMetadata {
  int32_t action_type = 0;       // e_DefString from Animation::GetAnimType(), NOT e_FunctionType
  float incoming_velocity = 0.0f;
  float outgoing_velocity = 0.0f;
  float incoming_body_angle = 0.0f;
  float outgoing_body_angle = 0.0f;
  float outgoing_angle = 0.0f;
  float difficulty = 0.0f;
  int32_t touch_frame = -1;
  int32_t quadrant = 0;
  bool last_ditch = false;
  bool base_animation = false;
  float idle_level = 0.0f;
  float special_var1 = 0.0f;
  float special_var2 = 0.0f;

  // Selection-only fields. Baked verbatim from the legacy getters so the
  // baked selector can reproduce CrudeSelection exactly.
  Vector3 incoming_body_direction;
  Vector3 outgoing_direction;
  Vector3 incoming_ball_direction;
  Vector3 outgoing_ball_direction;
  float incoming_ball_direction_max_deviation = 0.0f;
  float outgoing_ball_direction_max_deviation = 0.0f;
  int32_t trip_type = 0;
  int32_t current_foot = 0;
  std::string incoming_retain_state;
  std::string outgoing_retain_state;
  std::string incoming_special_state;
  std::string forced_foot;
  std::string touch_foot;

  // Runtime-only derived/string fields (B5b5/B5b7). Baked from legacy
  // string variables and motion getters that are not recoverable from
  // keyframes alone.
  std::string outgoing_special_state;
  std::string touch_bodypart;
  float touch_max_power_factor = 0.0f;
  float touch_difficulty_factor = 0.0f;
  float priority = 0.0f;
  Vector3 bump_direction;
  Vector3 incoming_movement;
  Vector3 outgoing_movement;
  Vector3 translation;
  int32_t outgoing_foot = 0;
};

// One contact touch (frame + ball position), mirroring
// FootballAnimationExtension::GetTouch.
struct BakedTouch {
  int32_t frame = 0;
  Vector3 position;
};

// Immutable runtime animation data, prepared once from the canonical GLB
// catalog and shared by all players.
struct AnimationClip {
  uint32_t id = 0;
  std::string name;
  uint32_t frame_count = 0;

  // Root motion, one position per frame (z already zeroed).
  std::vector<Vector3> root_positions;

  // Full per-frame joint pose.
  std::vector<PoseFrame> poses;

  FootballAnimationMetadata metadata;

  // Baked contact (first touch).
  bool has_contact = false;
  Vector3 contact_position;

  // Full touch list (frame + position).
  std::vector<BakedTouch> touches;

  // Derived at load time from the clip's own leg poses (see foot_support.hpp).
  // One entry per frame; empty means "not derived".
  std::vector<FootSupportFrame> foot_support;
  // How fast a stance foot travels backwards inside this clip, in metres per
  // second (stance-phase mean). 0 when the clip has no stance phase or no track.
  float stance_speed = 0.0f;
  // Per-phase detail and the quality flags; a caller must not trust the track
  // when `FootSupportTrusted()` is false.
  FootSupportSummary foot_support_summary;
  // RMA-1: measured root/support/loop quality of the clip, derived once at
  // load. It never changes the clip; it states whether the IK-free adaptation
  // may use this clip or must fall back to the compatibility path.
  ClipMotionQuality motion_quality;
};
