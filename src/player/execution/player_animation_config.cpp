#include "player/execution/player_animation_config.hpp"

#include <algorithm>
#include <cmath>

#include "football/animation/skeleton.hpp"
#include "football/player/execution/player_pose_adapter.hpp"
#include "player/execution/player_locomotion_animation.hpp"

namespace {

ClipTuning MakeClipTuning(const AnimationStyle& style) {
  ClipTuning tuning;
  tuning.stride_preference = style.stride_preference;
  tuning.cadence_preference = style.cadence_preference;
  tuning.stride_response = style.stride_response;
  tuning.pelvis_bob_scale = style.pelvis_bob_scale;
  tuning.torso_sway_scale = style.torso_sway_scale;
  tuning.arm_swing_scale = style.arm_swing_scale;
  return tuning;
}

// Ankle position relative to the hip for one leg, in world units, from local
// clip rotations and the declared bind table. No base yaw: the reach bound is
// rotation-invariant around the vertical, and the fore-aft axis is supplied
// separately.
football::math::Vector3 AnkleOffset(const PoseFrame& pose, BodyPart thigh,
                                    BodyPart knee, BodyPart ankle,
                                    float height_scale) {
  const football::math::Quaternion body_rotation = pose.orientations[BodyPart::body];
  const football::math::Vector3 hip =
      PlayerBindOffset(BodyPart::body) +
      body_rotation * PlayerBindOffset(thigh);
  const football::math::Quaternion thigh_rotation =
      (body_rotation * pose.orientations[thigh]).GetNormalized();
  const football::math::Vector3 knee_position =
      hip + thigh_rotation * PlayerBindOffset(knee);
  const football::math::Quaternion knee_rotation =
      (thigh_rotation * pose.orientations[knee]).GetNormalized();
  const football::math::Vector3 ankle_position =
      knee_position + knee_rotation * PlayerBindOffset(ankle);
  return (ankle_position - hip) * height_scale;
}

// One player-and-clip stride ceiling: the tightest fore-aft scale at which both
// legs can still reach their ankle through the whole clip. Computed once, from
// the clip's own poses - the style may ask for more, the body cannot give it.
float ComputeMaxStrideScale(const AnimationClip& clip, float height_scale) {
  if (clip.poses.empty() || !(height_scale > 0.0f)) {
    return kLocomotionMaxStrideScale;
  }
  const float thigh =
      PlayerBindOffset(BodyPart::left_knee).GetLength() * height_scale;
  const float shin =
      PlayerBindOffset(BodyPart::left_ankle).GetLength() * height_scale;
  const float reach = thigh + shin;
  const football::math::Vector3 facing(0, -1, 0);
  float ceiling = kLocomotionMaxStrideScale;
  for (const PoseFrame& pose : clip.poses) {
    for (const auto& leg : {std::pair<BodyPart, BodyPart>{BodyPart::left_thigh,
                                                         BodyPart::left_ankle},
                            {BodyPart::right_thigh, BodyPart::right_ankle}}) {
      const BodyPart knee = leg.first == BodyPart::left_thigh
                                ? BodyPart::left_knee
                                : BodyPart::right_knee;
      const football::math::Vector3 offset =
          AnkleOffset(pose, leg.first, knee, leg.second, height_scale);
      const float limit =
          LegStrideReachLimit(offset, offset, facing, reach);
      // LegStrideReachLimit takes hip/ankle; passing the offset as both makes it
      // operate on a hip at the origin, which is what we have here.
      ceiling = std::min(ceiling, limit);
    }
  }
  return std::clamp(ceiling, kLocomotionMinStrideScale, kLocomotionMaxStrideScale);
}

ClipTuning SanitizeClipTuning(ClipTuning tuning) {
  AnimationStyle style;
  style.stride_preference = tuning.stride_preference;
  style.cadence_preference = tuning.cadence_preference;
  style.stride_response = tuning.stride_response;
  style.pelvis_bob_scale = tuning.pelvis_bob_scale;
  style.torso_sway_scale = tuning.torso_sway_scale;
  style.arm_swing_scale = tuning.arm_swing_scale;
  return MakeClipTuning(SanitizeAnimationStyle(style));
}

}  // namespace

PlayerAnimationConfig BuildPlayerAnimationConfig(
    const football::player::PlayerDefinition& definition,
    const football::player::PlayerBodyProfile& body,
    const AnimationLibrary& library,
    const AnimationClipOverrideMap* overrides) {

  const AnimationStyle style = SanitizeAnimationStyle(
      definition.animation_style.value_or(AnimationStyle{}));
  const ClipTuning base = MakeClipTuning(style);

  const auto& clips = library.Clips();
  PlayerAnimationConfig config;
  config.clips.assign(clips.size(), base);
  config.library_clip_count = clips.size();
  // Per-clip reach ceiling: fixed for this player and this bake.
  for (std::size_t id = 0; id < clips.size(); ++id) {
    config.clips[id].max_stride_scale =
        ComputeMaxStrideScale(clips[id], body.height_scale);
  }

  if (overrides == nullptr || overrides->empty()) return config;
  // Resolve stable names to the ids of *this* library. An unknown name is
  // ignored rather than fatal: overrides may come from persisted data authored
  // against another bake.
  for (std::size_t id = 0; id < clips.size(); ++id) {
    const auto found = overrides->find(clips[id].name);
    if (found == overrides->end()) continue;
    config.clips[id] = SanitizeClipTuning(found->second);
  }
  return config;
}
