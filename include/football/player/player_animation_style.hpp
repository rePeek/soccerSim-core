#ifndef FOOTBALL_SIM_PLAYER_PLAYER_ANIMATION_STYLE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_ANIMATION_STYLE_HPP

#include <algorithm>
#include <cmath>
#include <optional>

// Per-player animation style: how this player makes the *shared* standard clips
// his own. It is static input, decided before the match, and never mutated by
// play - the runtime counterpart is `PlayerAnimationConfig` (one tuning per
// clip), built once from this style.
//
// These are style knobs, not abilities:
//   - `physical_acceleration` continues to bound the real acceleration;
//   - `height`/`weight` continue to set the skeleton scale and build;
//   - nothing here can change the Player's world displacement, the touch frame
//     or a contact impulse.
struct AnimationStyle {
  // Preferred stride length relative to the standard clip. 1.0 = as authored.
  float stride_preference = 1.0f;
  // Preferred cadence relative to the standard clip. 1.0 = as authored.
  float cadence_preference = 1.0f;
  // Share of a speed change absorbed by stride length instead of cadence.
  // 0 = cadence only, 1 = stride only.
  float stride_response = 0.5f;

  // Cosmetic pose scale. `pelvis_bob_scale` is applied to the vertical root bob;
  // the torso/arm terms are carried by the configuration and reserved for the
  // pose stage (applying them as a blanket joint-rotation scale would distort
  // the gait, so they are not guessed at here).
  float pelvis_bob_scale = 1.0f;
  float torso_sway_scale = 1.0f;
  float arm_swing_scale = 1.0f;
};

// Named starting points. Users may override any field afterwards.
enum class AnimationStylePreset {
  Natural,
  LongStrideLowCadence,
  ShortStrideHighCadence,
};

inline AnimationStyle MakeAnimationStyle(AnimationStylePreset preset) {
  AnimationStyle style;
  switch (preset) {
    case AnimationStylePreset::LongStrideLowCadence:
      style.stride_preference = 1.1f;
      style.cadence_preference = 0.9f;
      break;
    case AnimationStylePreset::ShortStrideHighCadence:
      style.stride_preference = 0.85f;
      style.cadence_preference = 1.15f;
      break;
    case AnimationStylePreset::Natural:
    default:
      break;
  }
  return style;
}

// Accepted ranges. A style outside them is clamped rather than rejected: it is
// authored data, and a player with a silly style must still play football.
inline constexpr float kAnimationStrideMin = 0.5f;
inline constexpr float kAnimationStrideMax = 2.0f;
inline constexpr float kAnimationCadenceMin = 0.5f;
inline constexpr float kAnimationCadenceMax = 2.0f;
inline constexpr float kAnimationPoseScaleMin = 0.5f;
inline constexpr float kAnimationPoseScaleMax = 1.5f;

// Deterministic and idempotent: invalid values are replaced by the neutral
// value, out-of-range values are clamped.
inline AnimationStyle SanitizeAnimationStyle(AnimationStyle style) {
  const auto sane = [](float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
  };
  style.stride_preference = std::clamp(
      sane(style.stride_preference, 1.0f), kAnimationStrideMin,
      kAnimationStrideMax);
  style.cadence_preference = std::clamp(
      sane(style.cadence_preference, 1.0f), kAnimationCadenceMin,
      kAnimationCadenceMax);
  style.stride_response =
      std::clamp(sane(style.stride_response, 0.5f), 0.0f, 1.0f);
  style.pelvis_bob_scale = std::clamp(
      sane(style.pelvis_bob_scale, 1.0f), kAnimationPoseScaleMin,
      kAnimationPoseScaleMax);
  style.torso_sway_scale = std::clamp(
      sane(style.torso_sway_scale, 1.0f), kAnimationPoseScaleMin,
      kAnimationPoseScaleMax);
  style.arm_swing_scale = std::clamp(
      sane(style.arm_swing_scale, 1.0f), kAnimationPoseScaleMin,
      kAnimationPoseScaleMax);
  return style;
}

inline bool operator==(const AnimationStyle& a, const AnimationStyle& b) {
  return a.stride_preference == b.stride_preference &&
         a.cadence_preference == b.cadence_preference &&
         a.stride_response == b.stride_response &&
         a.pelvis_bob_scale == b.pelvis_bob_scale &&
         a.torso_sway_scale == b.torso_sway_scale &&
         a.arm_swing_scale == b.arm_swing_scale;
}

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_ANIMATION_STYLE_HPP
