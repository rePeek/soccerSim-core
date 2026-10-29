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
// girth_scale:  continuous collider-width factor. 1.0 at the reference BMI (23);
//               below it the radii narrow, above it they widen. Shape.
struct PlayerBodyProfile {
  float height_m = 1.8f;
  float height_scale = 1.0f;
  float girth_scale = 1.0f;
};

// The baked skeleton is authored at this height, so a player of this height has
// height_scale == 1. Asset fact, not a definition default.
inline constexpr float kReferencePlayerHeight = 1.92f;

// Pure, deterministic, RNG-free. Rejects NaN, infinity and non-positive inputs.
//
// The width of the body follows BMI continuously: girth_scale = sqrt(bmi / 23).
// A radius scales as sqrt(area), so it tracks the square root of the relative
// mass rather than the mass. 23 is the reference BMI: a player at BMI 23 has
// girth_scale 1.
inline PlayerBodyProfile MakePlayerBodyProfile(
    const PlayerDefinition& definition) {
  const float height = definition.height;
  if (!std::isfinite(height) || height <= 0.0f)
    throw std::invalid_argument("player height must be finite and positive");

  const float weight = definition.weight;
  if (!std::isfinite(weight) || weight <= 0.0f)
    throw std::invalid_argument("player weight must be finite and positive");

  const float height_scale = height / kReferencePlayerHeight;
  const float bmi = weight / (height * height);
  const float girth_scale = std::sqrt(bmi / 23.0f);

  return PlayerBodyProfile{height, height_scale, girth_scale};
}

}  // namespace football::player

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP