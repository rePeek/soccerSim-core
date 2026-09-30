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

#include <cstdint>
#include <string>
#include <vector>

#include "animation/simanim_format.hpp"
#include "foundation/math/vector3.hpp"

using namespace blunted;

// Immutable runtime animation data. Baked ahead of time by anim_baker; the
// simulation never parses source assets.
struct AnimationClip {
  uint32_t id = 0;
  std::string name;
  uint32_t frame_count = 0;

  // Root motion, one position per frame (z already zeroed).
  std::vector<Vector3> root_positions;

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