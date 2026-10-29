// Player-role values: where a player lines up and what the referee expects of
// that role. A neutral match-level value: formation data, Referee rules,
// Event/Observation projections and the Player all consume it without any one
// of them owning it.

#ifndef FOOTBALL_SIM_MATCH_PLAYER_ROLE_HPP
#define FOOTBALL_SIM_MATCH_PLAYER_ROLE_HPP

constexpr int kPlayersPerTeam = 11;

enum e_PlayerRole {
  e_PlayerRole_GK,
  e_PlayerRole_CB,
  e_PlayerRole_LB,
  e_PlayerRole_RB,
  e_PlayerRole_DM,
  e_PlayerRole_CM,
  e_PlayerRole_LM,
  e_PlayerRole_RM,
  e_PlayerRole_AM,
  e_PlayerRole_CF,
};

#endif  // FOOTBALL_SIM_MATCH_PLAYER_ROLE_HPP
