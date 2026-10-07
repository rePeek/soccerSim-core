#ifndef FOOTBALL_SIM_PLAYER_TICK_CONTEXT_HPP
#define FOOTBALL_SIM_PLAYER_TICK_CONTEXT_HPP

#include "sim/time/tick.hpp"
#include "sim/random/rng.hpp"

class Player;
class Ball;

namespace football::sim {
// Stack-local input for a single Player call. Never retained by an actor.
// Opening contact may start the half during that call: fatigue reads the live
// clock gate AFTER Humanoid execution, not a pre-contact frozen snapshot.
struct PlayerTickContext {
  Tick now;
  bool play_authorized;
  const bool& half_underway;
  const Player* last_touch_player;
  const Ball& ball;
  blunted::SimulationRng& rng;
};
} // namespace football::sim

#endif
