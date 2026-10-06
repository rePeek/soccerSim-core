// Copyright 2026
#ifndef FOOTBALL_SIM_PLAYER_ACTION_EXECUTOR_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_EXECUTOR_HPP

#include <algorithm>
#include <cassert>
#include <limits>

#include "sim/player/player_action.hpp"

struct PlayerActionDefinition {
  e_FunctionType type = e_FunctionType_None;
  football::sim::TickSpan duration{};
  std::optional<football::sim::TickSpan> contact;
  Vector3 contactPosition = Vector3(0);
};

struct PlayerActionStepResult {
  bool contactTriggered = false;
  bool completed = false;
};

class PlayerActionExecutor {
 public:
  static void Begin(PlayerActionState& state,
                    const PlayerActionDefinition& definition) {
    assert(definition.duration.value > 0);
    assert(definition.duration.value <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
    assert(!definition.contact || *definition.contact <= definition.duration);
    state.type = definition.type;
    state.elapsed = {};
    state.duration = definition.duration;
    state.contact = definition.contact;
    state.contactPosition = definition.contactPosition;
  }

  static PlayerActionStepResult Step(PlayerActionState& state,
                                    football::sim::TickSpan delta) {
    assert(delta.value > 0);
    assert(state.duration.value > 0 && state.elapsed <= state.duration);
    const auto previous = state.elapsed;
    // Bound before adding: a large advance must saturate, not wrap the cursor.
    state.elapsed += std::min(delta, state.duration - state.elapsed);
    PlayerActionStepResult result;
    result.contactTriggered = state.contact && previous < *state.contact &&
                              state.elapsed >= *state.contact;
    result.completed = previous < state.duration && state.elapsed >= state.duration;
    return result;
  }
};

#endif
