// Copyright 2026
#ifndef FOOTBALL_SIM_PLAYER_ACTION_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_HPP

#include <optional>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_control.hpp"
#include "player/state/player_command_status.hpp"

using namespace football::math;

// The elapsed tick cursor is action time authority, independent of timeline
// fast-forwards. A baked animation frame is one tick; frame accessors are index
// projections, never additional timing state.
struct PlayerActionState {
  e_FunctionType type = e_FunctionType_None;
  football::sim::TickSpan elapsed{};
  football::sim::TickSpan duration{};
  std::optional<football::sim::TickSpan> contact;
  Vector3 contactPosition = Vector3(0);

  int Frame() const { return static_cast<int>(elapsed.value); }
  int FrameCount() const { return static_cast<int>(duration.value); }
  int ContactFrame() const { return contact ? static_cast<int>(contact->value) : -1; }

  bool HasScheduledContact() const { return contact.has_value(); }
  bool IsContactPending() const { return contact && elapsed < *contact; }
  bool IsContactDue() const { return contact && elapsed >= *contact; }
  bool IsComplete() const {
    return duration.value > 0 && elapsed >= duration;
  }
  bool IsAtLastFrame() const {
    return duration.value > 0 &&
           elapsed >= duration - football::sim::TickSpan{1};
  }

  // Procedural locomotion replaces root motion only for ordinary running.
  // Contact actions, retained ball and non-Movement actions keep baked motion.
  bool IsPureLocomotion(bool retains_ball) const {
    return type == e_FunctionType_Movement && !HasScheduledContact() && !retains_ball;
  }
};

// Execution feedback for the last discrete Command attempt. Command ->
// animation is not one-to-one, so both the source Command and the concrete
// e_FunctionType are exposed. A failed build leaves installed=false and
// carries command_status; `interrupted` means a new action replaced an
// incomplete one (a natural last-frame switch is not an interruption).
struct PlayerActionFeedback {
  ControlAction source_action = ControlAction::None;
  // Status of the Command build that produced (or failed to produce) this
  // execution. Accepted means the discrete action was executable.
  football::sim::player::CommandBuildStatus command_status =
      football::sim::player::CommandBuildStatus::MovementOnly;
  e_FunctionType type = e_FunctionType_None;
  bool installed = false;
  // The action reached its scheduled animation contact frame. This is not
  // proof that the ball was within reach or that a touch was accepted.
  bool contact_due = false;
  // Player produced an active-touch proposal for this action.
  bool touch_prepared = false;
  // Runtime accepted a touch by this player at or after prepared_touch_tick.
  bool touch_accepted = false;
  bool completed = false;
  bool interrupted = false;
  int start_frame = -1;
  int touch_frame = -1;
  football::sim::Tick prepared_touch_tick{};
};

#endif
