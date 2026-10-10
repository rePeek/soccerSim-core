#ifndef FOOTBALL_PLAYER_BAKED_BODY_POSE_HPP
#define FOOTBALL_PLAYER_BAKED_BODY_POSE_HPP
#include <array>
#include <cmath>
#include <stdexcept>
#include "sim/animation/clip.hpp"
#include "sim/animation/types.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_body_collider_motion.hpp"

// PoseFrame contains LOCAL keyframe rotations, not 14 world-space joint points.
// Animation::Apply rotates joints while retaining bind translations from
// data/media/objects/players/player.object. Only the player root's keyframe
// position is applied. These versioned bind offsets are a model declaration;
// runtime never parses the object or imports offline nodes/meshes.
struct BakedBodyPose {
  std::array<blunted::Vector3, kBodyPartCount> joints;
  std::array<blunted::Quaternion, kBodyPartCount> orientations;
};
inline BakedBodyPose EvaluateBakedBodyPose(const PoseFrame& pose,
    const PlayerKinematicState& state, float animation_base_angle, float scale = 1.0f) {
  if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(animation_base_angle))
    throw std::invalid_argument("invalid baked body pose transform");
  // No root XY duplication: procedural/animated PlayerState already contains it.
  // Baked vertical root displacement is NOT in the 2D PlayerState.
  BakedBodyPose out;
  out.joints[BodyPart::player] = state.position +
      blunted::Vector3(0, 0, pose.positions[BodyPart::player].coords[2] * scale);
  out.orientations[BodyPart::player] = blunted::Quaternion{};
  blunted::Quaternion base;
  base.SetAngleAxis(animation_base_angle, {0, 0, 1});
  const auto joint = [&](BodyPart child, BodyPart parent, blunted::Vector3 bind) {
    out.joints[child] = out.joints[parent] + out.orientations[parent] * (bind * scale);
    const auto local = child == BodyPart::body ? base * pose.orientations[child] : pose.orientations[child];
    out.orientations[child] = (out.orientations[parent] * local).GetNormalized();
  };
  joint(body, player, {0, 0, .96f});
  joint(middle, body, {0, 0, .15f});
  joint(neck, middle, {0, -.03f, .5f});
  joint(left_thigh, body, {.087f, 0, -.01f});
  joint(right_thigh, body, {-.087f, 0, -.01f});
  joint(left_knee, left_thigh, {0, 0, -.42f});
  joint(right_knee, right_thigh, {0, 0, -.42f});
  joint(left_ankle, left_knee, {0, -.04f, -.44f});
  joint(right_ankle, right_knee, {0, -.04f, -.44f});
  joint(left_shoulder, middle, {.16f, -.01f, .48f});
  joint(right_shoulder, middle, {-.16f, -.01f, .48f});
  joint(left_elbow, left_shoulder, {-.01f, 0, -.33f});
  joint(right_elbow, right_shoulder, {.01f, 0, -.33f});
  return out;
}

// Six anatomical capsules/sphere, not a fixed low-action proxy. Kept as a
// geometry proposal until rotation-changing CCD tapes and trajectory calibration
// are accepted; do not feed rotating capsules into translation-only CCD silently.
struct BakedPlayerBodyCollider {
  std::array<football::ball::ColliderShape, 6> shapes;
  std::array<PlayerBodyPart, 6> parts{
      PlayerBodyPart::UpperBody, PlayerBodyPart::Head,
      PlayerBodyPart::LowerBody, PlayerBodyPart::LowerBody,
      PlayerBodyPart::LowerBody, PlayerBodyPart::LowerBody};
};
inline BakedPlayerBodyCollider BuildBodyCollider(const PoseFrame& pose,
    const PlayerKinematicState& state, float animation_base_angle, float scale = 1.0f) {
  using namespace football::ball;
  const auto world = EvaluateBakedBodyPose(pose, state, animation_base_angle, scale);
  const auto& j = world.joints;
  return {{Capsule{j[body], j[neck], .20f * scale}, Sphere{j[neck], .11f * scale},
           Capsule{j[left_thigh], j[left_knee], .10f * scale},
           Capsule{j[left_knee], j[left_ankle], .09f * scale},
           Capsule{j[right_thigh], j[right_knee], .10f * scale},
           Capsule{j[right_knee], j[right_ankle], .09f * scale}}};
}
#endif
