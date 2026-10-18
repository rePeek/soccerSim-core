#ifndef FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP
#define FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"

using namespace football::math;

class Player;

// Referee-owned rule state: the values the rules must carry across ticks to
// relate facts over time. Physics and actors never read or write them; only the
// referee mutates them and Simulation applies the resulting rulings. Restart
// execution state lives in Runtime::RestartContext, never here.
struct Foul {
  Player *foulPlayer = 0;
  Player *foulVictim = 0;
  int foulType = 0; // 0: nothing, 1: foul, 2: yellow, 3: red
  bool advantage = false;
  football::sim::Tick foul_tick{};
  Vector3 foulPosition;
  bool hasBeenProcessed = false;
};

#endif  // FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP
