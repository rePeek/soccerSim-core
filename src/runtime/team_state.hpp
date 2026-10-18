#ifndef FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP

class Player;

namespace football::sim {

// Team-level match facts that cannot be derived from any single player. Runtime
// owns the storage; players and rules read a single-call borrowed TeamView and
struct TeamMatchState {
  bool has_possession = false;
  int time_to_ball_ms = 100;
  Player* designated_player = nullptr;
  float possession_amount = 0.0f;
  float fading_possession_amount = 0.0f;
};

// Runtime owns the only live orientation value; Team owns static templates.
// The three fields are distinct and must not be merged:
// - dynamic_side is the direction of the *current execution frame* (mirror
//   counter) and is what actors read;
// - static_side is the direction the team defends in the pitch, flipped only
//   by a change of ends;
// - mirrored is the current runtime coordinate-transform state (whether the
//   between-tick canonical frame is mirrored), which SwitchEnds must not touch.
struct TeamFrame {
  int dynamic_side = -1;
  int static_side = -1;
  bool mirrored = false;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
