#ifndef FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP

#include <cmath>
#include <stdexcept>

#include "football/player/player_definition.hpp"

namespace football::player {

// Static body geometry of one player, derived from its definition. This is the
// single source for height and girth scaling; it owns no motion capability
// (see PlayerMotionProfile) and no runtime state.
//
// height_scale: uniform skeleton scale relative to the baked asset's reference
//               height. Size.
// girth_scale:  the collider-width factor. 1.0 = reference build, 0.9 = slim,
//               1.1 = strong. The collider builders widen radii by this value,
//               partially per body part. Shape.
struct PlayerBodyProfile {
  float height_m = 1.8f;
  float height_scale = 1.0f;
  float girth_scale = 1.0f;
};

// The baked skeleton is authored at this height, so a player of this height has
// height_scale == 1. Asset fact, not a definition default.
inline constexpr float kReferencePlayerHeight = 1.92f;

// Build bands by BMI = weight / height^2 (kg / m^2). Only BMI strictly below
// kSlimBmi or strictly above kStrongBmi leaves the reference build; the two
// boundaries belong to the middle band.
inline constexpr float kSlimBmi = 20.0f;
inline constexpr float kStrongBmi = 25.0f;
inline constexpr float kSlimGirthScale = 0.9f;
inline constexpr float kStrongGirthScale = 1.1f;

inline float GirthScaleForBmi(float bmi) {
  if (bmi > kStrongBmi) return kStrongGirthScale;
  if (bmi < kSlimBmi) return kSlimGirthScale;
  return 1.0f;
}

// Pure, deterministic, RNG-free. Rejects NaN, infinity and non-positive inputs.
inline PlayerBodyProfile MakePlayerBodyProfile(
    const PlayerDefinition& definition) {
  const float height = definition.height;
  if (!std::isfinite(height) || height <= 0.0f)
    throw std::invalid_argument("player height must be finite and positive");

  const float weight = definition.weight_kg;
  if (!std::isfinite(weight) || weight <= 0.0f)
    throw std::invalid_argument("player weight must be finite and positive");

  const float height_scale = height / kReferencePlayerHeight;
  const float bmi = weight / (height * height);
  const float girth_scale = GirthScaleForBmi(bmi);

  return PlayerBodyProfile{height, height_scale, girth_scale};
}

}  // namespace football::player

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP
