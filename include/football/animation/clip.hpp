#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "football/animation/types.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

using namespace football::math;

// Canonical pose-array order follows BodyPart: middle..player.
constexpr size_t kBodyPartCount = 14;
static_assert(kBodyPartCount == BodyPart::body_part_max);

// Per-frame local joint pose in the canonical 14-joint skeleton.
struct PoseFrame {
  std::array<Quaternion, kBodyPartCount> orientations;
  std::array<Vector3, kBodyPartCount> positions;
};

// Typed canonical catalog metadata, including authored string-valued tags.
struct FootballAnimationMetadata {
  int32_t action_type = 0;       // e_DefString numeric catalog tag, NOT e_FunctionType
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

  // Authored movement/body/ball facts consumed by Player candidate selection.
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

  // Catalog facts not recoverable from the pose track alone. Import may
  // revalidate movement/translation when it installs a repaired gait.
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

// One authored contact event: frame and ball position in clip space.
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
  // RM-2: the clip is a cyclic gait. SampleAnimationStep() then measures root
  // deltas on the unwrapped timeline, so crossing the seam advances instead of
  // jumping back to the first sample.
  bool looping = false;

  // Full per-frame joint pose.
  std::vector<PoseFrame> poses;

  FootballAnimationMetadata metadata;

  // Baked contact (first touch).
  bool has_contact = false;
  Vector3 contact_position;

  // Full touch list (frame + position).
  std::vector<BakedTouch> touches;

  // Import-derived resource facts and measurements, not Player capability.
  // Per-frame support analysis lives only in the import workspace.
  ClipMotionQuality motion_quality;
};
