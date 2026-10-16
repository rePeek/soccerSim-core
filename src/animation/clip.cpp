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

// ANIM-2 A12.0: the .simanim clip (de)serialization bodies, moved verbatim out
// of clip.hpp. Field order and byte width are unchanged, so a baked artifact is
// byte-identical.

#include "football/animation/clip.hpp"

#include <istream>
#include <ostream>

#include "football/animation/format.hpp"

namespace {

void WriteVec3(std::ostream& os, const Vector3& v) {
  SimAnimWriteF32(os, v.coords[0]);
  SimAnimWriteF32(os, v.coords[1]);
  SimAnimWriteF32(os, v.coords[2]);
}

Vector3 ReadVec3(std::istream& is) {
  Vector3 v;
  v.coords[0] = SimAnimReadF32(is);
  v.coords[1] = SimAnimReadF32(is);
  v.coords[2] = SimAnimReadF32(is);
  return v;
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

FootballAnimationMetadata FootballAnimationMetadata::Deserialize(std::istream& is) {
  FootballAnimationMetadata m;
  m.action_type = SimAnimReadI32(is);
  m.incoming_velocity = SimAnimReadF32(is);
  m.outgoing_velocity = SimAnimReadF32(is);
  m.incoming_body_angle = SimAnimReadF32(is);
  m.outgoing_body_angle = SimAnimReadF32(is);
  m.outgoing_angle = SimAnimReadF32(is);
  m.difficulty = SimAnimReadF32(is);
  m.touch_frame = SimAnimReadI32(is);
  m.quadrant = SimAnimReadI32(is);
  m.last_ditch = SimAnimReadU32(is) != 0;
  m.base_animation = SimAnimReadU32(is) != 0;
  m.idle_level = SimAnimReadF32(is);
  m.special_var1 = SimAnimReadF32(is);
  m.special_var2 = SimAnimReadF32(is);
  m.incoming_body_direction = ReadVec3(is);
  m.outgoing_direction = ReadVec3(is);
  m.incoming_ball_direction = ReadVec3(is);
  m.outgoing_ball_direction = ReadVec3(is);
  m.incoming_ball_direction_max_deviation = SimAnimReadF32(is);
  m.outgoing_ball_direction_max_deviation = SimAnimReadF32(is);
  m.trip_type = SimAnimReadI32(is);
  m.current_foot = SimAnimReadI32(is);
  m.incoming_retain_state = SimAnimReadString(is);
  m.outgoing_retain_state = SimAnimReadString(is);
  m.incoming_special_state = SimAnimReadString(is);
  m.forced_foot = SimAnimReadString(is);
  m.touch_foot = SimAnimReadString(is);
  m.outgoing_special_state = SimAnimReadString(is);
  m.touch_bodypart = SimAnimReadString(is);
  m.touch_max_power_factor = SimAnimReadF32(is);
  m.touch_difficulty_factor = SimAnimReadF32(is);
  m.priority = SimAnimReadF32(is);
  m.bump_direction = ReadVec3(is);
  m.incoming_movement = ReadVec3(is);
  m.outgoing_movement = ReadVec3(is);
  m.translation = ReadVec3(is);
  m.outgoing_foot = SimAnimReadI32(is);
  return m;
}

void BakedTouch::Serialize(std::ostream& os) const {
  SimAnimWriteI32(os, frame);
  WriteVec3(os, position);
}

BakedTouch BakedTouch::Deserialize(std::istream& is) {
  BakedTouch t;
  t.frame = SimAnimReadI32(is);
  t.position = ReadVec3(is);
  return t;
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

AnimationClip AnimationClip::Deserialize(std::istream& is, uint32_t id) {
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
  const uint32_t touch_count = SimAnimReadU32(is);
  clip.touches.reserve(touch_count);
  for (uint32_t t = 0; t < touch_count; ++t) {
    clip.touches.push_back(BakedTouch::Deserialize(is));
  }
  return clip;
}
