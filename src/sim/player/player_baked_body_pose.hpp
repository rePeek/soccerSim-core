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

// Fixed anatomical segments, distinct physical identities even when several
// segments map to the same rule-facing PlayerBodyPart. Slots are Simulation's
// permanent roster slots, never traversal order or PlayerId.
enum class BakedBodySegment : std::uint8_t { Torso, Head, LeftThigh, LeftShin, RightThigh, RightShin };
inline constexpr std::size_t kBakedBodySegmentCount = 6;
inline football::ball::ColliderId BakedBodyColliderId(std::uint32_t slot, BakedBodySegment segment) {
  const auto index = static_cast<std::uint8_t>(segment);
  const std::uint64_t id = std::uint64_t(kFirstBakedBodyColliderId) +
      std::uint64_t(slot)*kBakedBodySegmentCount + index;
  if (index >= kBakedBodySegmentCount || id > std::numeric_limits<football::ball::ColliderId>::max())
    throw std::out_of_range("baked body collider exceeds reserved id range or segment count");
  return static_cast<football::ball::ColliderId>(id);
}
struct BakedPlayerBodyCollider {
  std::array<football::ball::ColliderShape, kBakedBodySegmentCount> shapes;
  std::array<PlayerBodyPart, kBakedBodySegmentCount> parts{
      PlayerBodyPart::UpperBody, PlayerBodyPart::Head,
      PlayerBodyPart::LowerBody, PlayerBodyPart::LowerBody,
      PlayerBodyPart::LowerBody, PlayerBodyPart::LowerBody};
};
// Read-only offline envelope calibration against the shipped local ASE vertices:
// head (222 vertices), upperleg (207), lowerleg (224). No runtime mesh loading.
// These cover those named meshes, not arms/boots; active boot/hand geometry is
// a separate P4f-3 gate, not a claim that six primitives cover the whole avatar.
inline const blunted::Vector3 kBakedHeadCenter{.0003f,-.0121f,.1212f};
inline constexpr float kBakedHeadRadius=.172f;
inline constexpr float kBakedThighRadius=.106f;
inline constexpr float kBakedShinRadius=.102f;
inline BakedPlayerBodyCollider BuildBodyCollider(const PoseFrame& pose,
    const PlayerKinematicState& state, float animation_base_angle, float scale = 1.0f) {
  using namespace football::ball;
  const auto world = EvaluateBakedBodyPose(pose, state, animation_base_angle, scale);
  const auto& j = world.joints;
  return {{Capsule{j[body], j[neck], .20f * scale},
           Sphere{j[neck]+world.orientations[neck]*(kBakedHeadCenter*scale),kBakedHeadRadius*scale},
           Capsule{j[left_thigh], j[left_knee], kBakedThighRadius * scale},
           Capsule{j[left_knee], j[left_ankle], kBakedShinRadius * scale},
           Capsule{j[right_thigh], j[right_knee], kBakedThighRadius * scale},
           Capsule{j[right_knee], j[right_ankle], kBakedShinRadius * scale}}};
}

// Both actual pose endpoints are supplied by the caller. Physics uses the
// endpoint/radius-linear contract, not an inferred rigid SLERP between poses.
// Production admission still requires P4f-2 continuity and P4f-3 calibration.
inline std::array<football::ball::ColliderMotion, kBakedBodySegmentCount> BuildBakedBodyColliderMotions(
    const BakedPlayerBodyCollider& start, const BakedPlayerBodyCollider& end, std::uint32_t fixed_slot,
    football::ball::ContactMaterial material = kPlayerBodyContactMaterial) {
  std::array<football::ball::ColliderMotion, kBakedBodySegmentCount> out;
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i] = {BakedBodyColliderId(fixed_slot, static_cast<BakedBodySegment>(i)),
              start.shapes[i], end.shapes[i], material};
  return out;
}

#endif
