#ifndef FOOTBALL_SIM_PLAYER_ACTION_MOTION_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_MOTION_HPP

#include <span>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/motion_clip.hpp"
#include "football/animation/types.hpp"
#include "football/player/execution/player_motion.hpp"
#include "football/player/player_kinematics.hpp"

// Baked action/reference-motion model. Unlike continuous PlayerMotion, these
// execution helpers need baked Animation data.
struct ActionMotionParameters {
  float max_velocity = 0.0f;
  float stat_agility = 0.0f;
  float stat_acceleration = 0.0f;
  float stat_velocity = 0.0f;
  float stat_dribble = 0.0f;
  float stat_balance = 0.0f;
  float stat_technical_ballcontrol = 0.0f;
  float last_touch_bias_1000 = 0.0f;
  float decaying_position_offset_length = 0.0f;
};

struct ActionMotionInput {
  Vector3 spatial_movement{0};
  radian angle = 0.0f;
  Vector3 direction_vec{0, -1, 0};
  float float_velocity = 0.0f;
  bool use_desired_movement = false;
  Vector3 desired_movement{0};
  bool use_desired_body_direction = false;
  Vector3 desired_body_direction_rel{0, -1, 0};
  ActionMotionParameters parameters;
};

struct ActionMotionPlan {
  std::vector<Vector3> positions;
  Vector3 outgoing_velocity{0};
  radian rotation_offset = 0.0f;
};

struct ActionMotionVectorInput {
  Vector3 start_position{0};
  radian start_angle = 0.0f;
  std::span<const Vector3> positions{};
  int frame_num = 0;
  Vector3 action_smuggle_offset{0};
  Vector3 action_smuggle_sustain_offset{0};
  Vector3 movement_smuggle_offset{0};
  Vector3 baked_pose_position{0};
  Vector3 previous_position_2d{0};
  Vector3 action_smuggle_movement{0};
  Vector3 movement_smuggle_movement{0};
  Vector3 position_offset_movement{0};
  std::span<const Vector3> root_positions{};
};

struct ActionMotionVectorResult {
  Vector3 position{0};
  Vector3 actual_movement{0};
  Vector3 physics_movement{0};
  Vector3 anim_movement{0};
  Vector3 movement{0};
  float float_velocity = 0.0f;
  e_Velocity enum_velocity = e_Velocity_Idle;
};

struct ActionBodyStateInput {
  Vector3 movement{0};
  radian start_angle = 0.0f;
  radian rotation_smuggle_offset = 0.0f;
  Quaternion baked_body_orientation;
};

struct ActionBodyStateResult {
  Vector3 direction_vec{0, -1, 0};
  radian angle = 0.0f;
  float float_velocity = 0.0f;
  e_Velocity enum_velocity = e_Velocity_Idle;
  Vector3 rel_body_direction_vec{0, -1, 0};
  Vector3 rel_body_direction_vec_nonquantized{0, -1, 0};
  radian rel_body_angle = 0.0f;
  radian rel_body_angle_nonquantized = 0.0f;
  Vector3 body_direction_vec{0, -1, 0};
  radian body_angle = 0.0f;
};

struct ActionSmuggleTickInput {
  int frame_num = 0;
  int touch_frame = -1;
  int frame_count = 0;
  int start_frame = 0;
  Vector3 action_smuggle{0};
  Vector3 movement_smuggle{0};
  radian rotation_smuggle_begin = 0.0f;
  radian rotation_smuggle_end = 0.0f;
  bool allow_pre_touch_rotation_smuggle = false;
};

struct ActionSmuggleTickResult {
  Vector3 action_smuggle_movement{0};
  Vector3 action_smuggle_offset_delta{0};
  Vector3 movement_smuggle_movement{0};
  Vector3 movement_smuggle_offset_delta{0};
  radian rotation_smuggle_offset = 0.0f;
};

class PlayerActionMotion {
 public:
  static football::math::Vector3 ClipDisplacement(const MotionClip& clip,
                                                  float frame);
  static MotionSample SampleClip(const MotionClip& clip, float frame, float dt);
  static ActionMotionPlan BuildActionMotionPlan(const AnimationClip& clip,
                                                const ActionMotionInput& input);
  static ActionMotionVectorResult ActionMotionVector(
      const ActionMotionVectorInput& input);
  static ActionBodyStateResult ActionBodyState(
      const ActionBodyStateInput& input);
  static ActionSmuggleTickResult ActionSmuggleTick(
      const ActionSmuggleTickInput& input);
};

#endif  // FOOTBALL_SIM_PLAYER_ACTION_MOTION_HPP
