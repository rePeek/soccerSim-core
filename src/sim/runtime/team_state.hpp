#ifndef FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP

class Player;

namespace football::sim {

// Team-level match facts that cannot be derived from any single player. Runtime
// owns the storage; players and rules read it through a borrowed TeamView and
// may update the few fields their own action legitimately changes (a pass
// designates its receiver; a restart placement sets the possession weight).
// This is a value, not a Team entity: no roster, no tactics, no behaviour.
struct TeamMatchState {
  bool has_possession = false;
  int time_to_ball_ms = 100;
  Player* designated_player = nullptr;
  float possession_amount = 0.0f;
  float fading_possession_amount = 0.0f;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
