// Rules-owned game-mode values: the states of play the referee moves between
// (open play, kick-off, restarts). Owned by the rules domain; actors only read
// the mode as a match fact.

#ifndef FOOTBALL_SIM_REFEREE_GAME_MODE_HPP
#define FOOTBALL_SIM_REFEREE_GAME_MODE_HPP

enum e_GameMode {
  e_GameMode_Normal,
  e_GameMode_KickOff,
  e_GameMode_GoalKick,
  e_GameMode_FreeKick,
  e_GameMode_Corner,
  e_GameMode_ThrowIn,
  e_GameMode_Penalty,
};

#endif  // FOOTBALL_SIM_REFEREE_GAME_MODE_HPP
