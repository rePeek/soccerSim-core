#ifndef FOOTBALL_SIM_PLAYER_PLAYER_ACTION_REQUEST_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_ACTION_REQUEST_HPP

#include <cstdint>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "sim/player/player_action.hpp"
#include "sim/player/player_definition.hpp"

// ANIM-1.1 / ANIM-2 A11.5/A11.7 — explicit per-player action request / reaction
// contract.
//
// The control model is deliberately narrow:
//   SetControl()  -> continuous movement intent (PlayerMotion authority).
//   Play(request)-> discrete actions only (pass/shoot/tackle/...).
//   React(...)    -> passive physics reactions (trips/bumps).
// `Play(ActionRequestKind::Move)` is rejected: a Movement clip selected through
// Play would not change the authoritative locomotion intent.
//
// Both funnel through one arbitration so the accept/defer/reject reason is always
// recorded. An accepted request/reaction is a real execution input: the next
// action tick consumes it and runs it through the one Player execution pipeline
// (command build -> selection -> Animation::Match -> install). A rejected or
// deferred request is not retained; the producer must submit it again when the
// arbitration window allows. Only the ACTION reaction is deferred here; the
// physical collision impulse was already applied by the contact phase.

enum class ActionRequestKind : std::uint8_t {
  None = 0,
  Move,
  Shoot,
  ShortPass,
  LongPass,
  HighPass,
  Dribble,
  Trap,
  Tackle,
  Save,
};

inline e_FunctionType ActionRequestFunctionType(ActionRequestKind kind) {
  switch (kind) {
    case ActionRequestKind::Move: return e_FunctionType_Movement;
    case ActionRequestKind::Shoot: return e_FunctionType_Shot;
    case ActionRequestKind::ShortPass: return e_FunctionType_ShortPass;
    case ActionRequestKind::LongPass: return e_FunctionType_LongPass;
    case ActionRequestKind::HighPass: return e_FunctionType_HighPass;
    case ActionRequestKind::Dribble: return e_FunctionType_BallControl;
    case ActionRequestKind::Trap: return e_FunctionType_Trap;
    case ActionRequestKind::Tackle: return e_FunctionType_Sliding;
    case ActionRequestKind::Save: return e_FunctionType_Deflect;
    case ActionRequestKind::None: return e_FunctionType_None;
  }
  return e_FunctionType_None;
}

struct PlayerActionRequest {
  ActionRequestKind kind = ActionRequestKind::None;
  // Movement direction or shot/kick direction, in the actor's RUNTIME frame.
  // The execution boundary converts it once into the value/command domain.
  football::math::Vector3 direction = football::math::Vector3(0);
  // Movement speed for a Move request; ignored by the one-shot actions.
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
};

// Active arbitration. A one-shot (non-Movement) action in flight rejects a
// repeat of the same action (no restart from repeated input) and defers a
// different one until it completes or reaches an interruptible window.
inline RequestOutcome ArbitrateActionRequest(const PlayerActionState& current,
                                             ActionRequestKind kind) {
  if (kind == ActionRequestKind::None) return RequestOutcome::Rejected;
  if (current.type != e_FunctionType_Movement && !current.IsComplete()) {
    if (current.type == ActionRequestFunctionType(kind)) {
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
