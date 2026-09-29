#ifndef _HPP_CORE_MODEL_WORLD_STATE
#define _HPP_CORE_MODEL_WORLD_STATE

#include <array>

#include "../../defines.hpp"
#include "ball/ball.hpp"
#include "match_state.hpp"
#include "player/player.hpp"

// Schema and ownership slot for the football world state (Phase 6).
//
// Pure simulation data: no Physics*/Controller*/RulesEngine*/Match*/Humanoid*/
// Team* pointers. This is the container that later phases (Physics / Action /
// Rules / Replay) read from and write to.
//
// BallState and the first 11 PlayerStates per team are runtime-owned here.
// Additional bench players and officials retain local state; MatchState is
// still migrating. The legacy save stream serializes via the existing facades.
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

#endif  // _HPP_CORE_MODEL_WORLD_STATE