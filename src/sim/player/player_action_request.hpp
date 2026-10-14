#ifndef FOOTBALL_SIM_PLAYER_PLAYER_ACTION_REQUEST_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_ACTION_REQUEST_HPP

#include <cstdint>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "sim/player/player_action.hpp"
#include "sim/player/player_definition.hpp"

// ANIM-1.1 — explicit per-player action request / reaction contract.
//
// `Player::Play(request)` submits an ACTIVE action for one player; it does not
// change the pose immediately. `Player::React(reaction)` submits a PASSIVE
// physics reaction with preemption priority. Both funnel through the same
// arbitration so the accept/defer/reject reason is always recorded. The value
// types here are the stable external vocabulary; the legacy
// PlayerControl/PlayerCommand path stays the execution source until the later
// ANIM-1 cuts wire the request into the runtime.

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
  // Movement direction or shot/kick direction (runtime frame).
  football::math::Vector3 direction = football::math::Vector3(0);
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
