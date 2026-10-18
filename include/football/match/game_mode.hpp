// Rules-owned game-mode values. They live in a neutral match-value header so
// actors (Player, observation) can name the current mode without depending on
// the Referee implementation module. Referee keeps owning the transitions.
#ifndef FOOTBALL_MATCH_GAME_MODE_HPP
#define FOOTBALL_MATCH_GAME_MODE_HPP

enum e_GameMode {
  e_GameMode_Normal,
  e_GameMode_KickOff,
  e_GameMode_GoalKick,
  e_GameMode_FreeKick,
  e_GameMode_Corner,
  e_GameMode_ThrowIn,
  e_GameMode_Penalty,
};

#endif  // FOOTBALL_MATCH_GAME_MODE_HPP
