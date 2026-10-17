// Player-role values: where a player lines up and what the referee expects of
// that role. Owned by the player domain; rules, AI and formation data consume
// it but do not define it.

#ifndef FOOTBALL_SIM_PLAYER_PLAYER_ROLE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_ROLE_HPP

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

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_ROLE_HPP
