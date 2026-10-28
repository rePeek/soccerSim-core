#ifndef FOOTBALL_SIM_PLAYER_PLAYER_ACTION_REQUEST_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_ACTION_REQUEST_HPP

#include <cstdint>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "player/state/player_action.hpp"
#include "football/player/player_definition.hpp"
#include "player/command/player_action_mapping.hpp"

// ANIM-1.1 / ANIM-2 A11.5/A11.7 — explicit per-player action request / reaction
// contract.
//
// The control model is deliberately narrow:
//   SetControl()  -> continuous movement intent (PlayerMotion authority).
//   Play(request)-> discrete actions only (pass/shoot/tackle/...).
//   React(...)    -> passive physics reactions (trips/bumps).
// `Play(...)` is for discrete actions only; continuous movement uses SetControl
// with ControlAction::None. Both entry points share the same ControlAction verb
// and the same BuildPlayerCommands translation boundary.
//
// Both funnel through one arbitration so the accept/defer/reject reason is always
// recorded. An accepted request/reaction is a real execution input: the next
// action tick consumes it and runs it through the one Player execution pipeline
// (command build -> selection -> Animation::Match -> install). A rejected or
// deferred request is not retained; the producer must submit it again when the
// arbitration window allows. Only the ACTION reaction is deferred here; the
// physical collision impulse was already applied by the contact phase.

// Nominal execution family for arbitration. ThroughPass resolves to short/long
// from its target distance during command build; this value only identifies the
// family for duplicate detection.
inline e_FunctionType ActionRequestFunctionType(ControlAction action) {
  if (action == ControlAction::None) return e_FunctionType_None;
  return football::sim::player::ControlActionFunctionType(action);
}

struct PlayerActionRequest {
  ControlAction action = ControlAction::None;
  // Movement direction or shot/kick direction, in the actor's RUNTIME frame.
  // The execution boundary converts it once into the value/command domain.
  football::math::Vector3 direction = football::math::Vector3(0);
  // Movement speed or the desired approach speed for a discrete action; the
  // action's own power is carried separately in `power`.
  float speed = 0.0f;
  // Absolute target position on the pitch for passes/shots (optional).
  football::math::Vector3 target_position = football::math::Vector3(0);
  bool has_target_position = false;
  football::player::PlayerId target_player = football::player::kInvalidPlayerId;
  float power = 0.0f;
};

enum class ReactionKind : std::uint8_t {
  None = 0,
  Trip,
  Bump,
};

struct PlayerReaction {
  ReactionKind kind = ReactionKind::None;
  football::math::Vector3 direction = football::math::Vector3(0);
  float severity = 0.0f;
  football::player::PlayerId source = football::player::kInvalidPlayerId;
  int trip_type = 0;
};

// Why a request was accepted, deferred or rejected. Recorded for regression
// diagnostics; never inferred from the animation id alone.
enum class RequestOutcome : std::uint8_t {
  Accepted = 0,
  Deferred,
  Rejected,
  // Defined Command, but no execution path exists in the current build.
  Unsupported,
};

// Active arbitration. A one-shot (non-Movement) action in flight rejects a
// repeat of the same action (no restart from repeated input) and defers a
// different one until it completes or reaches an interruptible window.
// Pull and CallSupport are defined Commands with no Player execution path:
// report Unsupported rather than accepting and silently doing something else.
inline RequestOutcome ArbitrateActionRequest(const PlayerActionState& current,
                                             ControlAction action) {
  if (action == ControlAction::None) return RequestOutcome::Rejected;
  if (action == ControlAction::Pull ||
      action == ControlAction::CallSupport)
    return RequestOutcome::Unsupported;
  if (current.type != e_FunctionType_Movement && !current.IsComplete()) {
    if (current.type == ActionRequestFunctionType(action)) {
      return RequestOutcome::Rejected;  // already running this one-shot
    }
    return RequestOutcome::Deferred;    // wait for a legal switch window
  }
  return RequestOutcome::Accepted;
}

// Passive arbitration: a real physics reaction always preempts the current
// action; the kind of interruption is a later ANIM-1.4 decision.
inline RequestOutcome ArbitrateReaction(const PlayerActionState& /*current*/,
                                        ReactionKind kind) {
  if (kind == ReactionKind::None) return RequestOutcome::Rejected;
  return RequestOutcome::Accepted;
}

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_ACTION_REQUEST_HPP
