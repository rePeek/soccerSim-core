// Actor-readable team match facts. Runtime owns the full TeamMatchState; this
// base subobject is borrowed live, so a same-tick designation or possession
// change is visible immediately. Player never holds a Team.
#ifndef FOOTBALL_PLAYER_PLAYER_TEAM_MATCH_FACTS_HPP
#define FOOTBALL_PLAYER_PLAYER_TEAM_MATCH_FACTS_HPP

class Player;

namespace football::player {

struct PlayerTeamMatchFacts {
  bool has_possession = false;
  int time_to_ball_ms = 100;
  Player* designated_player = nullptr;
};

}  // namespace football::player

#endif  // FOOTBALL_PLAYER_PLAYER_TEAM_MATCH_FACTS_HPP
