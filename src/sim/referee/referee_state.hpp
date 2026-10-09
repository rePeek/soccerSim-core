#ifndef FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP
#define FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP

#include <optional>
#include <vector>

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/football_types.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/referee/restart_readiness.hpp"

using namespace blunted;

class Player;
class Team;

// Referee-owned rule state: the values the rules must carry across ticks to
// relate facts over time. Physics and actors never read or write them; only the
// referee mutates them and Simulation applies the resulting commands/rulings.

struct RestartPolicy {
  football::sim::TickSpan minimum_delay;
  football::sim::TickSpan maximum_delay;
};
// Explicit restart-scheduling facts; the referee state machine reads no Match here.
struct RestartSchedule {
  football::sim::Tick now{};
  Team* setpiece_team = nullptr;
  PitchFrameTransform frame{false};
};
enum class RestartPhase { Pending, Ready, Taken, InPlay };
struct RestartState {
  football::sim::Tick entered_tick{};
  football::sim::Tick earliest_restart_tick{};
  football::sim::Tick timeout_tick{};
  RestartPhase phase = RestartPhase::Pending;
  bool setup_done = false;
  bool used_timeout_placement = false;
  RestartPlan plan;
};

struct RefereeBuffer {
  // Referee has pending action to execute.
  bool active = false;
  e_GameMode desiredSetPiece;
  signed int teamID = 0;
  Team* setpiece_team = 0;
  football::sim::Tick stop_tick{};
  football::sim::Tick prepare_tick{};
  football::sim::Tick start_tick{};
  Vector3 restartPos;
  Player *taker = nullptr;
  bool endPhase = false;
  std::optional<RestartState> restart;
};

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
