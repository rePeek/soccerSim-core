// Actor-readable team match facts. Runtime owns the full TeamMatchState; this
// base subobject is borrowed live, so a same-tick designation or possession
// change is visible immediately. Player never holds a Team.
#ifndef FOOTBALL_PLAYER_PLAYER_TEAM_MATCH_FACTS_HPP
#define FOOTBALL_PLAYER_PLAYER_TEAM_MATCH_FACTS_HPP

#include "football/player/player_definition.hpp"

namespace football::player {

struct PlayerTeamMatchFacts {
  bool has_possession = false;
  int time_to_ball_ms = 100;
  // Team possession weights. Runtime owns the only writable TeamMatchState;
  // actors read these through the borrowed PlayerTeamView.
  float possession_amount = 0.0f;
  float fading_possession_amount = 0.0f;
  // Stable identity of the designated possession player. Runtime keeps the
  // resolved actor next to it and hands that to live consumers; identity-only
  // consumers read this value, so the contract stays snapshot/AI-safe instead
  // of carrying an object reference.
  PlayerId designated_player = kInvalidPlayerId;
};

}  // namespace football::player

#endif  // FOOTBALL_PLAYER_PLAYER_TEAM_MATCH_FACTS_HPP
