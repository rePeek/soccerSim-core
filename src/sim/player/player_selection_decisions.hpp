#ifndef FOOTBALL_SIM_PLAYER_SELECTION_DECISIONS_HPP
#define FOOTBALL_SIM_PLAYER_SELECTION_DECISIONS_HPP

// ANIM-1 A6.4 step 6: the ReQueue gates that used to be inline in
// `Humanoid::SelectAnim`, as explicit, inspectable value inputs. These are pure
// decisions: no RNG, no state mutation, no candidate search. `Player` owns them
// (`Player::CanAttemptActionSelection` / `CanCommitActionSelection`); the
// Humanoid still supplies the not-yet-migrated legacy facts through the context
// structs. The value logic lives here so it can be unit-tested without linking
// the actor runtime.

#include <cmath>

#include "football/animation/types.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/foundation/math/vector3.hpp"
#include "sim/event/touch_type.hpp"
#include "sim/player/player_command.hpp"

namespace football::sim {

// Step 6a: the gate evaluated *before* the candidate search. It rejects an
// action selection attempt that must not even look for candidates.
struct RequeueGateContext {
  // The action currently installed (a projection of PlayerActionState).
  e_FunctionType action_type = e_FunctionType_None;
  int action_frame = 0;
  int action_frame_count = 0;
  int action_contact_frame = -1;

  // The command the installed action originated from, and the candidate command.
  PlayerCommand originating_command;
  PlayerCommand command;

  // Motion facts of the current tick (the legacy Humanoid spatial state).
  Vector3 spatial_movement{0};
  Vector3 outgoing_movement{0};
  Vector3 incoming_movement{0};

  bool has_possession = false;
  football::sim::Tick last_touch_tick{};
  e_TouchType last_touch_type = e_TouchType_None;
  float focus_distance = 0.0f;

  int requeue_delay_frames = 0;
  bool allow_movement_requeue = true;
  bool allow_ball_control_requeue = true;
  bool allow_trap_requeue = true;
  int max_ball_control_requeue_frame = 0;
  int min_remaining_movement_requeue_frames = 0;
  int min_remaining_trap_requeue_frames = 0;

  football::sim::Tick now{};
};

// Step 6b: the gate evaluated *after* the candidate search, on the selected
// clip. It must run at the exact same point as the legacy check, because the
// search it follows can contain conditional work and RNG.
struct CommitSelectionContext {
  bool is_requeue = false;
  bool has_selection = false;
  bool current_positions_sufficient = false;
  bool selected_positions_sufficient = false;

  e_FunctionType action_type = e_FunctionType_None;
  e_FunctionType command_type = e_FunctionType_None;

  e_Velocity current_outgoing_velocity = e_Velocity_Idle;
  e_Velocity selected_outgoing_velocity = e_Velocity_Idle;
  int current_quadrant = 0;
  int selected_quadrant = 0;
  radian current_forced_outgoing_angle = 0.0f;
  radian selected_forced_outgoing_angle = 0.0f;
};

// Same 1.5 m/s momentum-gap criterion the scheduler uses to call a candidate
// "too similar to what we are already trying to accomplish".
inline constexpr float kRequeueMaterialCommandMomentumGap = 1.5f;

// Returns true when the candidate search may run for this ReQueue attempt.
inline bool AttemptActionSelectionAllowed(const RequeueGateContext &context) {
  if (context.action_type != e_FunctionType_Movement &&
      context.command.desiredFunctionType == e_FunctionType_Movement) {
    return false;
  }
  if (context.action_type == e_FunctionType_Movement &&
      context.command.desiredFunctionType == e_FunctionType_Movement &&
      (context.has_possession || context.focus_distance > 12.0f)) {
    return false;
  }
  if (context.action_type == e_FunctionType_Movement &&
      context.command.desiredFunctionType == e_FunctionType_Movement &&
      context.action_frame + context.min_remaining_movement_requeue_frames >
          context.action_frame_count - 1) {
    return false;
  }
  if (context.action_type == e_FunctionType_Movement &&
      context.command.desiredFunctionType == e_FunctionType_Movement &&
      (!context.allow_movement_requeue || context.requeue_delay_frames > 0)) {
    return false;
  }
  if (context.action_type == e_FunctionType_BallControl &&
      context.command.desiredFunctionType == e_FunctionType_BallControl &&
      (!context.allow_ball_control_requeue ||
       context.action_frame > context.max_ball_control_requeue_frame ||
       context.requeue_delay_frames > 0)) {
    return false;
  }
  if (context.action_type == e_FunctionType_BallControl &&
      context.command.desiredFunctionType == e_FunctionType_Trap) {
    return false;
  }
  if (context.action_type == e_FunctionType_Trap &&
      context.command.desiredFunctionType == e_FunctionType_Trap &&
      (!context.allow_trap_requeue ||
       context.action_frame + context.min_remaining_trap_requeue_frames >
           context.action_contact_frame ||
       context.requeue_delay_frames > 0)) {
    return false;
  }
  if (context.action_type == e_FunctionType_Trap &&
      context.command.desiredFunctionType == e_FunctionType_BallControl &&
      (!context.allow_trap_requeue ||
       context.action_frame + context.min_remaining_trap_requeue_frames >
           context.action_contact_frame ||
       context.requeue_delay_frames > 0)) {
    return false;
  }

  // too similar to what we are already trying to accomplish
  if (context.originating_command.desiredFunctionType ==
          context.command.desiredFunctionType &&
      ((context.originating_command.desiredDirection *
        context.originating_command.desiredVelocityFloat) -
       (context.command.desiredDirection * context.command.desiredVelocityFloat))
              .GetLength() < kRequeueMaterialCommandMomentumGap) {
    return false;
  }

  // requeue not needed?
  if ((context.action_type == e_FunctionType_Movement &&
       context.command.desiredFunctionType == e_FunctionType_Movement) ||
      (context.action_type == e_FunctionType_BallControl &&
       context.command.desiredFunctionType == e_FunctionType_BallControl) ||
      (context.action_type == e_FunctionType_Trap &&
       context.command.desiredFunctionType == e_FunctionType_BallControl) ||
      (context.action_type == e_FunctionType_Trap &&
       context.command.desiredFunctionType == e_FunctionType_Trap)) {
    // current change in momentum
    Vector3 plannedMomentumChange =
        context.outgoing_movement - context.incoming_movement;
    Vector3 desiredMomentumChange =
        (context.command.desiredDirection * context.command.desiredVelocityFloat) -
        context.spatial_movement;

    if ((desiredMomentumChange.GetDotProduct(plannedMomentumChange) > 0.0f &&
         desiredMomentumChange.GetDistance(plannedMomentumChange) < 4.0f) ||
        desiredMomentumChange.GetDotProduct(plannedMomentumChange) > 0.8f ||
        desiredMomentumChange.GetDistance(plannedMomentumChange) < 2.0f) {
      return false;
    }
  }

  // don't requeue movement to ballcontrol halfway movement anims, unless there's
  // a serious change of movement desired
  if (context.action_type == e_FunctionType_Movement &&
      context.command.desiredFunctionType == e_FunctionType_BallControl &&
      (context.now - context.last_touch_tick < football::sim::TickSpan{60} &&
       context.last_touch_type == e_TouchType_Intentional_Kicked) &&
      context.has_possession) {
    // && !CastPlayer()->AllowLastDitch()) {
    float desiredMovementChange =
        (context.spatial_movement -
         (context.command.desiredDirection * context.command.desiredVelocityFloat))
            .GetLength();
    if (desiredMovementChange < 1.0f) return false;
  }

  return true;
}

// Returns true when the selected action may be committed. A false answer means
// the caller must drop the selection (legacy `selectedAnimID = -1`).
inline bool CommitActionSelectionAllowed(const CommitSelectionContext &context) {
  if (!context.is_requeue || !context.has_selection ||
      !context.current_positions_sufficient ||
      !context.selected_positions_sufficient) {
    return true;
  }

  // don't requeue to same quadrant
  if (context.action_type == context.command_type) {
    const bool both_non_idle_same_quadrant =
        context.current_outgoing_velocity != e_Velocity_Idle &&
        context.current_quadrant == context.selected_quadrant;
    const bool both_idle_same_direction =
        context.current_outgoing_velocity == e_Velocity_Idle &&
        context.selected_outgoing_velocity == e_Velocity_Idle &&
        std::fabs(context.current_forced_outgoing_angle -
                  context.selected_forced_outgoing_angle) < 0.06f * pi;
    if (both_non_idle_same_quadrant || both_idle_same_direction) {
      return false;
    }
  }

  return true;
}

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_SELECTION_DECISIONS_HPP
