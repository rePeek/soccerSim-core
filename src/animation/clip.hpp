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

  // Scalar metadata (typed-metadata migration lands in a later stage).
  float incoming_velocity = 0.0f;
  float outgoing_velocity = 0.0f;
  float incoming_body_angle = 0.0f;
  float outgoing_body_angle = 0.0f;
  float anim_difficulty = 0.0f;
  int32_t touch_frame = 0;
  int32_t quadrant_id = 0;
  int32_t anim_type = 0;

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
    SimAnimWriteF32(os, incoming_velocity);
    SimAnimWriteF32(os, outgoing_velocity);
    SimAnimWriteF32(os, incoming_body_angle);
    SimAnimWriteF32(os, outgoing_body_angle);
    SimAnimWriteF32(os, anim_difficulty);
    SimAnimWriteI32(os, touch_frame);
    SimAnimWriteI32(os, quadrant_id);
    SimAnimWriteI32(os, anim_type);
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
    clip.incoming_velocity = SimAnimReadF32(is);
    clip.outgoing_velocity = SimAnimReadF32(is);
    clip.incoming_body_angle = SimAnimReadF32(is);
    clip.outgoing_body_angle = SimAnimReadF32(is);
    clip.anim_difficulty = SimAnimReadF32(is);
    clip.touch_frame = SimAnimReadI32(is);
    clip.quadrant_id = SimAnimReadI32(is);
    clip.anim_type = SimAnimReadI32(is);
    return clip;
  }
};

#endif