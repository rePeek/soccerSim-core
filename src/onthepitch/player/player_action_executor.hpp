//
//  player_action_executor.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_ACTION_EXECUTOR
#define _HPP_PLAYER_ACTION_EXECUTOR

#include <algorithm>
#include "player_action.hpp"

// A simulation-owned action schedule. Humanoid animation metadata is still
// adapted into PlayerActionState during the transition, but new actions can be
// created with this definition without an Animation or Scene3D object.
struct PlayerActionDefinition {
  e_FunctionType type = e_FunctionType_None;
  int durationTime_ms = 0;
  int contactTime_ms = -1;
  Vector3 contactPosition = Vector3(0);
};

class PlayerActionExecutor {
 public:
  static void Begin(PlayerActionState &state,
                    const PlayerActionDefinition &definition) {
    DO_VALIDATION;
    assert(definition.durationTime_ms > 0);
    assert(definition.durationTime_ms % 10 == 0);
    assert(definition.contactTime_ms == -1 ||
           (definition.contactTime_ms >= 0 &&
            definition.contactTime_ms <= definition.durationTime_ms &&
            definition.contactTime_ms % 10 == 0));

    state.type = definition.type;
    state.frame = 0;
    state.frameCount = definition.durationTime_ms / 10;
    state.elapsedTime_ms = 0;
    state.durationTime_ms = definition.durationTime_ms;
    state.contactTime_ms = definition.contactTime_ms;
    state.contactFrame = definition.contactTime_ms == -1
                             ? -1
                             : definition.contactTime_ms / 10;
    state.contactPosition = definition.contactPosition;
  }

  static void Step(PlayerActionState &state, int dt_ms) {
    DO_VALIDATION;
    assert(dt_ms > 0);
    assert(dt_ms % 10 == 0);
    assert(state.durationTime_ms > 0);

    state.elapsedTime_ms =
        std::min(state.elapsedTime_ms + dt_ms, state.durationTime_ms);
    state.frame = state.elapsedTime_ms / 10;
  }
};

#endif
