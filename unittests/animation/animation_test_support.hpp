#ifndef FOOTBALL_ANIMATION_TEST_SUPPORT_HPP
#define FOOTBALL_ANIMATION_TEST_SUPPORT_HPP

#include <string>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"

// Shared synthetic-clip helpers for the pure Animation unit tests. Small
// hand-built clips let a test pin an exact frame/joint pose; the real baked
// library is used separately in unittests/loco_animation.

inline PoseFrame PoseWith(float root_z, float body_angle_deg) {
  PoseFrame pose;
  pose.positions[BodyPart::player] = football::math::Vector3(0.0f, 0.0f, root_z);
  pose.orientations[BodyPart::body].SetAngleAxis(
      body_angle_deg * football::math::pi / 180.0f, football::math::Vector3(0, 0, 1));
  return pose;
}

// A Movement clip whose root travels `displacement` metres over `frames`.
inline AnimationClip MakeMovementClip(uint32_t id, float incoming, float outgoing,
                                      float turn_deg, float displacement,
                                      uint32_t frames = 25) {
  AnimationClip clip;
  clip.id = id;
  clip.name = "synth" + std::to_string(id);
  clip.frame_count = frames;
  clip.root_positions = {football::math::Vector3(0, 0, 0),
                         football::math::Vector3(0, -displacement, 0)};
  clip.poses.resize(frames);
  clip.metadata.action_type = e_DefString_Movement;
  clip.metadata.incoming_velocity = incoming;
  clip.metadata.outgoing_velocity = outgoing;
  clip.metadata.incoming_body_direction = football::math::Vector3(0, -1, 0);
  clip.metadata.outgoing_direction = football::math::Vector3(0, -1, 0);
  clip.metadata.outgoing_body_angle = turn_deg * football::math::pi / 180.0f;
  clip.metadata.touch_frame = -1;
  return clip;
}

#endif  // FOOTBALL_ANIMATION_TEST_SUPPORT_HPP
