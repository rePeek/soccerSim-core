#ifndef FOOTBALL_SIM_PLAYER_PLAYER_MOTION_PROFILE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_MOTION_PROFILE_HPP

#include "football/player/player_definition.hpp"

// Public reference values. They mirror PlayerLocomotionParameters defaults and
// the baked sprint reference without exposing the locomotion implementation.
inline constexpr float kPlayerMotionSprintVelocity = 7.0f;
inline constexpr float kPlayerMotionDefaultAcceleration = 6.0f;
inline constexpr float kPlayerMotionDefaultBraking = 12.0f;
inline constexpr float kPlayerMotionDefaultTurnRate = 6.0f;

// Static movement capability of one player, derived from its definition.
//
// The reference profile: the ability -> capability conversion has
// one home and can be tested before the motion authority moves to Player.
//
// Deliberately excludes fatigue. It is a per-tick runtime modifier
// (Player::GetStat applies it today) and must not be baked into a static
// profile, or a tired player could never slow down.
struct PlayerMotionProfile {
  float max_speed = 7.5f;     // metres per second
  float acceleration = 6.0f;  // m/s^2 while speeding up
  float braking = 12.0f;      // m/s^2 while slowing down
  float turn_rate = 6.0f;     // rad/s at a standstill
};

// Ability-derived profile. `PlayerAttributes` defaults every stat to 1.0, and the
// ability factors below are 1.0 at that neutral value, so a default definition
// reproduces the current PlayerLocomotionParameters exactly. That keeps this a
// faithful extraction rather than a new physics model.
inline PlayerMotionProfile MakePlayerMotionProfile(
    const football::player::PlayerDefinition& definition) {
  const auto ability = [&definition](football::player::PlayerStat stat) {
    return definition.attributes.get(stat);
  };
  const float acceleration_factor =
      0.5f + 0.5f * ability(football::player::PlayerStat::physical_acceleration);
  PlayerMotionProfile profile;
  // Same formula as Player::GetMaxVelocity() at a neutral runtime multiplier.
  profile.max_speed =
      kPlayerMotionSprintVelocity *
      (0.9f + 0.1f * ability(football::player::PlayerStat::physical_velocity));
  profile.acceleration = kPlayerMotionDefaultAcceleration * acceleration_factor;
  profile.braking = kPlayerMotionDefaultBraking * acceleration_factor;
  profile.turn_rate =
      kPlayerMotionDefaultTurnRate *
      (0.5f + 0.5f * ability(football::player::PlayerStat::physical_agility));
  return profile;
}

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_MOTION_PROFILE_HPP
