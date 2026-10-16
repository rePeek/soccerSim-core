// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _HPP_ANIMATION_CLIP
#define _HPP_ANIMATION_CLIP

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

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

  // ANIM-2 A12.0: the byte-level bodies live in src/animation/clip.cpp.
  void Serialize(std::ostream& os) const;
  static FootballAnimationMetadata Deserialize(std::istream& is);
};

// One contact touch (frame + ball position), mirroring
// FootballAnimationExtension::GetTouch.
struct BakedTouch {
  int32_t frame = 0;
  Vector3 position;

  void Serialize(std::ostream& os) const;
  static BakedTouch Deserialize(std::istream& is);
};

// Immutable runtime animation data. Baked ahead of time by anim_baker; the
// simulation never parses source assets.
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

  // ANIM-2 A12.0: the byte-level bodies live in src/animation/clip.cpp.
  void Serialize(std::ostream& os) const;
  static AnimationClip Deserialize(std::istream& is, uint32_t id);
};

#endif
