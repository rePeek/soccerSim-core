#ifndef FOOTBALL_SIM_PLAYER_PLAYER_MOTION_PROFILE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_MOTION_PROFILE_HPP

#include "football/player/player_definition.hpp"
#include "player/player_locomotion.hpp"
#include "player/player_motion_constants.hpp"

// Static movement capability of one player, derived from its definition.
//
// The reference profile: the ability -> capability conversion has
// one home and can be tested before the motion authority moves to Player.
//
// Deliberately excludes AI difficulty and fatigue. Those are per-tick runtime
// modifiers (Player::GetStat applies them today) and must not be baked into a
// static profile, or a tired player could never slow down.
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
  const PlayerLocomotionParameters defaults;
  const auto ability = [&definition](football::player::PlayerStat stat) {
    return definition.attributes.get(stat);
  };
  const float acceleration_factor =
      0.5f + 0.5f * ability(football::player::PlayerStat::physical_acceleration);
  PlayerMotionProfile profile;
  // Same formula as Player::GetMaxVelocity() at a neutral runtime multiplier.
  profile.max_speed =
      animSprintVelocity *
      (0.9f + 0.1f * ability(football::player::PlayerStat::physical_velocity));
  profile.acceleration = defaults.acceleration * acceleration_factor;
  profile.braking = defaults.braking * acceleration_factor;
  profile.turn_rate =
      defaults.maxTurnRate *
      (0.5f + 0.5f * ability(football::player::PlayerStat::physical_agility));
  return profile;
}

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_MOTION_PROFILE_HPP
