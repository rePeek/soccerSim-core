#ifndef FOOTBALL_PLAYER_EXECUTION_ACTION_ARBITRATION_HPP
#define FOOTBALL_PLAYER_EXECUTION_ACTION_ARBITRATION_HPP

#include "football/player/player_action_request.hpp"
#include "football/player/player_action.hpp"
#include "player/execution/player_action_mapping.hpp"

// Nominal execution family for duplicate detection; the actual ThroughPass
// short/long choice still uses the explicit target distance during command build.
inline e_FunctionType ActionRequestFunctionType(ControlAction action) {
  if (action == ControlAction::None) return e_FunctionType_None;
  return football::sim::player::ControlActionFunctionType(action);
}

// Arbitration depends on the running action, so it is Execution, not input
// protocol validation. Deferred requests are not implicitly retained.
inline RequestOutcome ArbitrateActionRequest(const PlayerActionState& current,
                                             ControlAction action) {
  if (action == ControlAction::None) return RequestOutcome::Rejected;
  if (action == ControlAction::Pull ||
      action == ControlAction::CallSupport)
    return RequestOutcome::Unsupported;
  if (current.type != e_FunctionType_Movement && !current.IsComplete()) {
    if (current.type == ActionRequestFunctionType(action)) {
      return RequestOutcome::Rejected;
    }
    return RequestOutcome::Deferred;
  }
  return RequestOutcome::Accepted;
}

inline RequestOutcome ArbitrateReaction(const PlayerActionState& /*current*/,
                                        ReactionKind kind) {
  if (kind == ReactionKind::None) return RequestOutcome::Rejected;
  return RequestOutcome::Accepted;
}

#endif
