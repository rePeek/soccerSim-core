#ifndef FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP

#include <cmath>
#include <stdexcept>

#include "football/player/player_definition.hpp"

namespace football::player {

// Coarse visual/geometry label derived from the mass-to-standard-mass ratio. It
// is an output classification for tests and later Godot blend-shape selection;
// geometry must read the continuous girth_scale below, never this enum.
enum class BodyBuild {
  Slim,
  Normal,
  Broad,
};

// Static body shape of one player, derived from its definition. This is the
// single source for height and girth scaling. It owns no motion capability
// (see PlayerMotionProfile) and no runtime state.
struct PlayerBodyProfile {
  float height_m = 1.8f;
  float mass_kg = 0.0f;
  float height_scale = 1.0f;
  float girth_scale = 1.0f;
  BodyBuild build = BodyBuild::Normal;
};

// The single reference height at which height_scale == 1. Height scaling is
// expressed relative to it, so it must stay the only such constant.
inline constexpr float kReferencePlayerHeight = 1.92f;

// Classify by the mass-to-reference-mass ratio r = M / kDefaultPlayerMassKg.
// Thresholds are the slim/normal/broad bands around the reference build; they
// are never used to compute geometry.
inline constexpr float kSlimMassRatio = 0.9f;
inline constexpr float kBroadMassRatio = 1.1f;

inline BodyBuild ClassifyBodyBuild(float mass_ratio) {
  if (mass_ratio < kSlimMassRatio) return BodyBuild::Slim;
  if (mass_ratio > kBroadMassRatio) return BodyBuild::Broad;
  return BodyBuild::Normal;
}

// Pure, deterministic, RNG-free. Rejects NaN, infinity and non-positive inputs.
inline PlayerBodyProfile MakePlayerBodyProfile(
    const PlayerDefinition& definition) {
  const float height = definition.height;
  if (!std::isfinite(height) || height <= 0.0f)
    throw std::invalid_argument("player height must be finite and positive");

  const float height_scale = height / kReferencePlayerHeight;

  const float mass = definition.weight_kg;
  if (!std::isfinite(mass) || mass <= 0.0f)
    throw std::invalid_argument("player weight must be finite and positive");

  // Girth is a shape parameter, so it is the ratio to the reference mass and is
  // independent of height; size comes from height_scale. 1.0 is the reference
  // build. Geometry consumes this value, never the BodyBuild label.
  const float mass_ratio = mass / kDefaultPlayerMassKg;
  const float girth_scale = std::sqrt(mass_ratio);

  return PlayerBodyProfile{height, mass, height_scale, girth_scale,
                           ClassifyBodyBuild(mass_ratio)};
}

}  // namespace football::player

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP