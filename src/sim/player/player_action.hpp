// Copyright 2026
#ifndef FOOTBALL_SIM_PLAYER_ACTION_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_HPP

#include <optional>

#include "sim/animation/types.hpp"
#include "foundation/math/vector3.hpp"
#include "sim/time/tick.hpp"

using namespace blunted;

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

#endif
