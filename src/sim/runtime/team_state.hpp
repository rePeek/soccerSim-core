#ifndef FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP

class Player;

namespace football::sim {

// Team-level match facts that cannot be derived from any single player. Runtime
// owns the storage; players read it through a borrowed PlayerTeamView and may
// update the one field their own action legitimately changes (the receiver
// designated by a pass). This is a value, not a Team entity: it holds no
// roster, no tactics and no behaviour.
struct TeamMatchState {
  bool has_possession = false;
  int time_to_ball_ms = 100;
  Player* designated_player = nullptr;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
