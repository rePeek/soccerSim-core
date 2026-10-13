#ifndef FOOTBALL_PLAYER_BAKED_POSE_SAMPLE_HPP
#define FOOTBALL_PLAYER_BAKED_POSE_SAMPLE_HPP
#include "sim/player/player_baked_body_pose.hpp"
#include "sim/player/player_baked_contact_regions.hpp"

// Read-only sample of the current runtime animation slot, not a forecast and
// not another Humanoid execution. The requested frame is retained so a caller
// cannot silently mistake end-of-clip clamping for animation continuity.
struct PlayerBakedPoseSample {
  PoseFrame pose;
  PlayerKinematicState state;
  AnimationId animation_id = 0;
  int requested_frame = 0;
  int sampled_frame = 0;
  float base_angle = 0;
  football::player::PlayerBodyProfile body_profile;
  e_FunctionType action = e_FunctionType_None;
  bool frame_clamped = false;
  BakedPlayerBodyCollider Geometry() const {
    return BuildBodyCollider(pose,state,base_angle,body_profile);
  }
  auto ContactRegions() const {
    return BuildBakedContactRegions(pose,state,base_angle,body_profile);
  }
  void Mirror() {
    state.Mirror();
    // PlayerKinematicState::Mirror deliberately does not rotate facing fields;
    // FK takes the animation base explicitly, so only that angle is transformed.
    football::math::radian angle = base_angle;
    angle.Mirror(); base_angle = angle;
  }
};
#endif
