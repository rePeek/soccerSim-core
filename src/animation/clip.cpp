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

// Stable clip encoding used to hash decoded catalog content. Keep field order
// and byte widths unchanged so resource provenance survives IO cleanup.
// This is write-only in-memory encoding, not a resource loading format.

#include "football/animation/clip.hpp"

#include <ostream>

#include "football/animation/format.hpp"

namespace {

void WriteVec3(std::ostream& os, const Vector3& v) {
  SimAnimWriteF32(os, v.coords[0]);
  SimAnimWriteF32(os, v.coords[1]);
  SimAnimWriteF32(os, v.coords[2]);
}

}  // namespace

void FootballAnimationMetadata::Serialize(std::ostream& os) const {
  SimAnimWriteI32(os, action_type);
  SimAnimWriteF32(os, incoming_velocity);
  SimAnimWriteF32(os, outgoing_velocity);
  SimAnimWriteF32(os, incoming_body_angle);
  SimAnimWriteF32(os, outgoing_body_angle);
  SimAnimWriteF32(os, outgoing_angle);
  SimAnimWriteF32(os, difficulty);
  SimAnimWriteI32(os, touch_frame);
  SimAnimWriteI32(os, quadrant);
  SimAnimWriteU32(os, last_ditch ? 1u : 0u);
  SimAnimWriteU32(os, base_animation ? 1u : 0u);
  SimAnimWriteF32(os, idle_level);
  SimAnimWriteF32(os, special_var1);
  SimAnimWriteF32(os, special_var2);
  WriteVec3(os, incoming_body_direction);
  WriteVec3(os, outgoing_direction);
  WriteVec3(os, incoming_ball_direction);
  WriteVec3(os, outgoing_ball_direction);
  SimAnimWriteF32(os, incoming_ball_direction_max_deviation);
  SimAnimWriteF32(os, outgoing_ball_direction_max_deviation);
  SimAnimWriteI32(os, trip_type);
  SimAnimWriteI32(os, current_foot);
  SimAnimWriteString(os, incoming_retain_state);
  SimAnimWriteString(os, outgoing_retain_state);
  SimAnimWriteString(os, incoming_special_state);
  SimAnimWriteString(os, forced_foot);
  SimAnimWriteString(os, touch_foot);
  SimAnimWriteString(os, outgoing_special_state);
  SimAnimWriteString(os, touch_bodypart);
  SimAnimWriteF32(os, touch_max_power_factor);
  SimAnimWriteF32(os, touch_difficulty_factor);
  SimAnimWriteF32(os, priority);
  WriteVec3(os, bump_direction);
  WriteVec3(os, incoming_movement);
  WriteVec3(os, outgoing_movement);
  WriteVec3(os, translation);
  SimAnimWriteI32(os, outgoing_foot);
}

void BakedTouch::Serialize(std::ostream& os) const {
  SimAnimWriteI32(os, frame);
  WriteVec3(os, position);
}

void AnimationClip::Serialize(std::ostream& os) const {
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
  SimAnimWriteU32(os, static_cast<uint32_t>(touches.size()));
  for (const BakedTouch& t : touches) t.Serialize(os);
}
