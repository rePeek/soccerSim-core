#include "player/execution/player_animation_config.hpp"

#include <algorithm>

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
  // The body profile is part of the build contract (a style and a skeleton are
  // separate facts), but it must not silently change the style: height already
  // scales the reference geometry in the motion adapter.
  static_cast<void>(body);

  const AnimationStyle style = SanitizeAnimationStyle(
      definition.animation_style.value_or(AnimationStyle{}));
  const ClipTuning base = MakeClipTuning(style);

  const auto& clips = library.Clips();
  PlayerAnimationConfig config;
  config.clips.assign(clips.size(), base);

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
