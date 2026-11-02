#pragma once

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

// Value-only Player touch policy: action-specific reach tolerance, capability
// and last-touch bias. Animation supplies the contact positions, not this verdict.
namespace football::player {

using football::math::Vector3;
using football::math::radian;

// Preferred contact offset, with the Player's touch lead supplied explicitly.
Vector3 FrontOfFootOffsetRel(float velocity, radian bodyAngleRel, float height,
                             float touch_offset_ms);

struct TouchGeometryRequest {
  // All animation-space: the caller has already rotated by -spatial_angle.
  Vector3 anim_ball_pos{0};           // clip.touches[i].position
  Vector3 anim_body_pos{0};           // touchPose.positions[BodyPart::player]
  Vector3 touch_body_pos_rotated{0};  // positions_ret.rotated(-spatial_angle)
  Vector3 ball_pos{0};
  Vector3 ball_movement{0};
  Vector3 touch_movement{0};
  int anim_touch_frame = 0;
  int frame_count = 0;
  int default_touch_frame = 0;
  e_FunctionType function_type = e_FunctionType_None;
  bool is_base = false;
  bool ball_retained_by_player = false;
  bool allow_pre_touch_rotation_smuggle = false;
  bool use_continuous_ball_check = false;
  bool prefer_pass_and_shot = false;
  radian rotation_smuggle = 0.0f;
  radian body_rotation_z = 0.0f;
  float height_scale = 1.0f;
  float last_touch_bias = 0.0f;
  float technical_ballcontrol = 0.0f;
  float touch_offset_ms = 80.0f;
};

struct TouchGeometryResult {
  Vector3 anim_ball_pos{0};
  Vector3 ball_pos{0};
  Vector3 action_smuggle_vec2d{0};
  float ball_distance_z = 0.0f;
  // Only meaningful when `within_reach` is true.
  bool within_reach = false;
  float touch_framed_radius_factor = 0.0f;
  float radius_cheat_offset = 0.0f;
  Vector3 ffo{0};
};

TouchGeometryResult EvaluateTouchGeometry(const TouchGeometryRequest& request);

}  // namespace football::player
