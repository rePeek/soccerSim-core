#ifndef FOOTBALL_ANIMATION_TOUCH_GEOMETRY_HPP
#define FOOTBALL_ANIMATION_TOUCH_GEOMETRY_HPP

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"

// ANIM-1 A6.4 step 2b - stateless per-candidate touch geometry scoring.
//
// A line-for-line relocation of the pure part of the touch loop in
// `Humanoid::GetBestCheatableAnimID`: from the rotation-smuggle placement of the
// animation's ball contact through the vertical distance, the touch-frame
// radius scaling, the front-of-foot offset and the last-touch bias. Everything
// it needs travels in the request struct; it reads no Player / Humanoid /
// Simulation / Ball / RNG. `GetBodyBallDistanceAdvantage` stays on the Humanoid
// side and consumes this result.
//
// ANIM-2 A12.3: the bodies live in src/animation/touch_geometry.cpp.

namespace football::animation {

// Former foundation::Line, relocated so both Humanoid call sites share one
// definition. `u` is the unclamped parameter of `point` projected onto the
// segment [a, b]; it keeps the legacy 2D-projection / 3D-length formula exactly.
float SegmentProjectionParameter(const Vector3& a, const Vector3& b,
                                 const Vector3& point);

// `GetFrontOfFootOffsetRel`, with the touch offset made explicit instead of read
// from a sim-side constant.
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

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_TOUCH_GEOMETRY_HPP
