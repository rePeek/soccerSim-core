#ifndef _HPP_CORE_STATE_WORLD_STATE
#define _HPP_CORE_STATE_WORLD_STATE

#include <array>

#include "../../defines.hpp"
#include "ball_state.hpp"
#include "match_state.hpp"
#include "player_state.hpp"

// Schema and ownership slot for the football world state (Phase 6).
//
// Pure simulation data: no Physics*/Controller*/RulesEngine*/Match*/Humanoid*/
// Team* pointers. This is the container that later phases (Physics / Action /
// Rules / Replay) read from and write to.
//
// BallState is authoritative from Phase 7 onward (it lives here).
// PlayerState and MatchState are still migrating from the legacy model.
struct WorldState {
  unsigned long time_ms = 0;
  BallState ball;
  std::array<PlayerState, 2 * MAX_PLAYERS> players;
  MatchState match_state;

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(time_ms);
    ball.ProcessState(state);
    for (PlayerState &player : players) {
      player.ProcessState(state);
    }
    match_state.ProcessState(state);
  }
};

#endif  // _HPP_CORE_STATE_WORLD_STATE