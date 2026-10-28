#ifndef FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP

#include <cmath>
#include <stdexcept>

#include "football/player/state/player_definition.hpp"

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

// Reference BMI coefficient anchoring the normal-mass proxy M_normal(H) = B0*H^2.
// This is a geometric calibration parameter, not an anthropometric claim.
inline constexpr float kReferenceBmi = 23.0f;

// The single reference height at which height_scale == 1 (was the legacy
// defaultPlayerHeight; BODY-1.5 removes that duplicate).
inline constexpr float kReferencePlayerHeight = 1.92f;

// Classify by the mass-to-standard-mass ratio r = M / (B0*H^2). Thresholds map
// the BODY-1 nine-profile matrix (1.65/1.80/1.95 m × slim/normal/broad) onto
// the three labels; they are never used to compute geometry.
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

  float mass;
  float girth_scale;
  if (definition.weight_kg) {
    const float weight = *definition.weight_kg;
    if (!std::isfinite(weight) || weight <= 0.0f)
      throw std::invalid_argument("player weight must be finite and positive");
    mass = weight;
    const float normal_mass = kReferenceBmi * height * height;
    // Continuous relative-girth proxy: geometry consumes this value, not the
    // BodyBuild label derived from it.
    girth_scale = std::sqrt(mass / normal_mass);
  } else {
    // Legacy player without an explicit weight keeps standard girth for its
    // height, so adding the field never silently fattens or slims an actor.
    mass = kReferenceBmi * height * height;
    girth_scale = 1.0f;
  }

  const float mass_ratio = mass / (kReferenceBmi * height * height);
  return PlayerBodyProfile{height, mass, height_scale, girth_scale,
                           ClassifyBodyBuild(mass_ratio)};
}

}  // namespace football::player

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_BODY_PROFILE_HPP