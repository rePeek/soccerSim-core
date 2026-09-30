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
#include <string>
#include <vector>

#include "animation/simanim_format.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

using namespace blunted;

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
  int32_t action_type = 0;       // e_FunctionType
  float incoming_velocity = 0.0f;
  float outgoing_velocity = 0.0f;
  float incoming_body_angle = 0.0f;
  float outgoing_body_angle = 0.0f;
  float difficulty = 0.0f;
  int32_t touch_frame = -1;
  int32_t quadrant = 0;
  bool last_ditch = false;
  bool base_animation = false;
  float idle_level = 0.0f;
  float special_var1 = 0.0f;
  float special_var2 = 0.0f;

  void Serialize(std::ostream& os) const {
    SimAnimWriteI32(os, action_type);
    SimAnimWriteF32(os, incoming_velocity);
    SimAnimWriteF32(os, outgoing_velocity);
    SimAnimWriteF32(os, incoming_body_angle);
    SimAnimWriteF32(os, outgoing_body_angle);
    SimAnimWriteF32(os, difficulty);
    SimAnimWriteI32(os, touch_frame);
    SimAnimWriteI32(os, quadrant);
    SimAnimWriteU32(os, last_ditch ? 1u : 0u);
    SimAnimWriteU32(os, base_animation ? 1u : 0u);
    SimAnimWriteF32(os, idle_level);
    SimAnimWriteF32(os, special_var1);
    SimAnimWriteF32(os, special_var2);
  }

  static FootballAnimationMetadata Deserialize(std::istream& is) {
    FootballAnimationMetadata m;
    m.action_type = SimAnimReadI32(is);
    m.incoming_velocity = SimAnimReadF32(is);
    m.outgoing_velocity = SimAnimReadF32(is);
    m.incoming_body_angle = SimAnimReadF32(is);
    m.outgoing_body_angle = SimAnimReadF32(is);
    m.difficulty = SimAnimReadF32(is);
    m.touch_frame = SimAnimReadI32(is);
    m.quadrant = SimAnimReadI32(is);
    m.last_ditch = SimAnimReadU32(is) != 0;
    m.base_animation = SimAnimReadU32(is) != 0;
    m.idle_level = SimAnimReadF32(is);
    m.special_var1 = SimAnimReadF32(is);
    m.special_var2 = SimAnimReadF32(is);
    return m;
  }
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

  void Serialize(std::ostream& os) const {
    SimAnimWriteString(os, name);
    SimAnimWriteU32(os, frame_count);
    for (const Vector3& p : root_positions) {
      SimAnimWriteF32(os, p.coords[0]);
      SimAnimWriteF32(os, p.coords[1]);
      SimAnimWriteF32(os, p.coords[2]);
    }
    for (const PoseFrame& pose : poses) {
      for (size_t b = 0; b < kBodyPartCount; ++b) {
        for (int e = 0; e < 4; ++e) {
          SimAnimWriteF32(os, pose.orientations[b].elements[e]);
        }
        SimAnimWriteF32(os, pose.positions[b].coords[0]);
        SimAnimWriteF32(os, pose.positions[b].coords[1]);
        SimAnimWriteF32(os, pose.positions[b].coords[2]);
      }
    }
    metadata.Serialize(os);
    SimAnimWriteU32(os, has_contact ? 1u : 0u);
    if (has_contact) {
      SimAnimWriteF32(os, contact_position.coords[0]);
      SimAnimWriteF32(os, contact_position.coords[1]);
      SimAnimWriteF32(os, contact_position.coords[2]);
    }
  }

  static AnimationClip Deserialize(std::istream& is, uint32_t id) {
    AnimationClip clip;
    clip.id = id;
    clip.name = SimAnimReadString(is);
    clip.frame_count = SimAnimReadU32(is);
    clip.root_positions.reserve(clip.frame_count);
    for (uint32_t f = 0; f < clip.frame_count; ++f) {
      Vector3 p;
      p.coords[0] = SimAnimReadF32(is);
      p.coords[1] = SimAnimReadF32(is);
      p.coords[2] = SimAnimReadF32(is);
      clip.root_positions.push_back(p);
    }
    clip.poses.reserve(clip.frame_count);
    for (uint32_t f = 0; f < clip.frame_count; ++f) {
      PoseFrame pose;
      for (size_t b = 0; b < kBodyPartCount; ++b) {
        for (int e = 0; e < 4; ++e) {
          pose.orientations[b].elements[e] = SimAnimReadF32(is);
        }
        pose.positions[b].coords[0] = SimAnimReadF32(is);
        pose.positions[b].coords[1] = SimAnimReadF32(is);
        pose.positions[b].coords[2] = SimAnimReadF32(is);
      }
      clip.poses.push_back(std::move(pose));
    }
    clip.metadata = FootballAnimationMetadata::Deserialize(is);
    clip.has_contact = SimAnimReadU32(is) != 0;
    if (clip.has_contact) {
      clip.contact_position.coords[0] = SimAnimReadF32(is);
      clip.contact_position.coords[1] = SimAnimReadF32(is);
      clip.contact_position.coords[2] = SimAnimReadF32(is);
    }
    return clip;
  }
};

#endif