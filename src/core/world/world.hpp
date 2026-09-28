#ifndef _HPP_CORE_WORLD
#define _HPP_CORE_WORLD

class GameTask;  // legacy
#include "core/state/world_state.hpp"

// World is the future top-level simulation orchestrator.
//
// Phase 3: a simulation entry seam — it borrows the legacy GameTask and
// forwards Step() to it.
// Phase 6+: it owns the WorldState. BallState is authoritative here from
// Phase 7; PlayerState and MatchState still migrate from legacy.
class World {
public:
  // World borrows the legacy GameTask; it does not own it. The invariant
  // is: a World that exists always has a valid GameTask to step.
  explicit World(GameTask& game_task) : legacy_game_task_(game_task) {}

  // Advance the simulation by one phase (tick).
  void Step();

  // Ownership slot for the simulation state (Phase 6+). BallState is
  // authoritative here since Phase 7; the rest still migrates.
  WorldState& GetState() { return state_; }
  const WorldState& GetState() const { return state_; }

private:
  GameTask& legacy_game_task_;
  WorldState state_;
};

#endif  // _HPP_CORE_WORLD