// ANIM-1 A6.4 step 6: focused unit tests for the extracted ReQueue decisions.
// These are pure value functions, so they need no actor runtime, RNG or assets.

#include <catch2/catch_test_macros.hpp>

#include "player/execution/player_selection_decisions.hpp"

using football::sim::AttemptActionSelectionAllowed;
using football::sim::CommitActionSelectionAllowed;
using football::sim::CommitSelectionContext;
using football::sim::RequeueGateContext;

namespace {

RequeueGateContext MakeMovementRequeue() {
  RequeueGateContext c;
  c.action_type = e_FunctionType_Movement;
  c.action_frame = 0;
  c.action_frame_count = 10;
  c.min_remaining_movement_requeue_frames = 0;
  c.command.desiredFunctionType = e_FunctionType_Movement;
  // Skip the "too similar" check by default: the originating action differed.
  c.originating_command.desiredFunctionType = e_FunctionType_Trap;
  c.command.desiredDirection = Vector3(1, 0, 0);
  c.command.desiredVelocityFloat = 3.0f;
  c.spatial_movement = Vector3(0);
  return c;
}

}  // namespace

TEST_CASE("ReQueue gate 6a: movement cannot requeue while possessing",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.has_possession = true;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
  c.has_possession = false;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: far focus distance rejects movement requeue",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.focus_distance = 12.5f;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
  c.focus_distance = 11.0f;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: too few remaining frames rejects movement requeue",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.action_frame = 9;
  c.action_frame_count = 10;
  c.min_remaining_movement_requeue_frames = 1;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
  c.action_frame = 0;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: delay and disabled flag reject movement requeue",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.requeue_delay_frames = 1;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
  c.requeue_delay_frames = 0;
  c.allow_movement_requeue = false;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
  c.allow_movement_requeue = true;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: BallControl-to-Trap is always rejected",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.action_type = e_FunctionType_BallControl;
  c.command.desiredFunctionType = e_FunctionType_Trap;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: BallControl requeue frame budget is enforced",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.action_type = e_FunctionType_BallControl;
  c.command.desiredFunctionType = e_FunctionType_BallControl;
  c.max_ball_control_requeue_frame = 8;
  c.action_frame = 9;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
  c.action_frame = 8;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: Trap requeue stops shortly before contact",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.action_type = e_FunctionType_Trap;
  c.command.desiredFunctionType = e_FunctionType_Trap;
  c.min_remaining_trap_requeue_frames = 6;
  c.action_contact_frame = 10;
  c.action_frame = 5;  // 5 + 6 = 11 > 10
  CHECK_FALSE(AttemptActionSelectionAllowed(c));
  c.action_frame = 3;  // 3 + 6 = 9 <= 10
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: a materially similar command is rejected",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.originating_command.desiredFunctionType = e_FunctionType_Movement;
  c.originating_command.desiredDirection = Vector3(0);
  c.originating_command.desiredVelocityFloat = 0.0f;
  // Make the redundancy check pass so only the similarity gate can reject.
  c.outgoing_movement = Vector3(0);
  c.incoming_movement = Vector3(10, 0, 0);
  c.spatial_movement = Vector3(0);
  c.command.desiredDirection = Vector3(1, 0, 0);
  c.command.desiredVelocityFloat = 1.0f;  // 1.0 m/s gap -> too similar
  CHECK_FALSE(AttemptActionSelectionAllowed(c));

  // A gap of exactly the 1.5 m/s threshold is no longer "too similar".
  c.command.desiredVelocityFloat = 1.5f;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: a redundant momentum change is rejected",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  // planned and desired momentum both zero, so distance < 2.
  c.outgoing_movement = Vector3(0);
  c.incoming_movement = Vector3(0);
  c.spatial_movement = Vector3(0);
  c.command.desiredDirection = Vector3(0);
  c.command.desiredVelocityFloat = 0.0f;
  CHECK_FALSE(AttemptActionSelectionAllowed(c));

  // A large desired momentum change is a real requeue.
  c.command.desiredDirection = Vector3(1, 0, 0);
  c.command.desiredVelocityFloat = 5.0f;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("ReQueue gate 6a: a recent intentional kick blocks a sideways move",
          "[player][selection]") {
  RequeueGateContext c = MakeMovementRequeue();
  c.action_type = e_FunctionType_Movement;
  c.command.desiredFunctionType = e_FunctionType_BallControl;
  c.has_possession = true;
  c.last_touch_type = e_TouchType_Intentional_Kicked;
  c.last_touch_tick = c.now;
  c.command.desiredDirection = Vector3(0.1f, 0, 0);
  c.command.desiredVelocityFloat = 1.0f;
  c.spatial_movement = Vector3(0);
  CHECK_FALSE(AttemptActionSelectionAllowed(c));

  // The same command with no possession is allowed through this branch.
  c.has_possession = false;
  CHECK(AttemptActionSelectionAllowed(c));
}

TEST_CASE("Commit gate 6b: a same-quadrant non-idle requeue is dropped",
          "[player][selection]") {
  CommitSelectionContext c;
  c.is_requeue = true;
  c.has_selection = true;
  c.current_positions_sufficient = true;
  c.selected_positions_sufficient = true;
  c.action_type = e_FunctionType_Movement;
  c.command_type = e_FunctionType_Movement;
  c.current_outgoing_velocity = e_Velocity_Walk;
  c.current_quadrant = 2;
  c.selected_quadrant = 2;
  CHECK_FALSE(CommitActionSelectionAllowed(c));

  c.selected_quadrant = 3;
  CHECK(CommitActionSelectionAllowed(c));
}

TEST_CASE("Commit gate 6b: idle clips with the same preferred direction are dropped",
          "[player][selection]") {
  CommitSelectionContext c;
  c.is_requeue = true;
  c.has_selection = true;
  c.current_positions_sufficient = true;
  c.selected_positions_sufficient = true;
  c.action_type = e_FunctionType_Movement;
  c.command_type = e_FunctionType_Movement;
  c.current_outgoing_velocity = e_Velocity_Idle;
  c.selected_outgoing_velocity = e_Velocity_Idle;
  c.current_forced_outgoing_angle = 0.0f;
  c.selected_forced_outgoing_angle = 0.0f;
  CHECK_FALSE(CommitActionSelectionAllowed(c));

  c.selected_forced_outgoing_angle = 0.5f * pi;
  CHECK(CommitActionSelectionAllowed(c));
}

TEST_CASE("Commit gate 6b: non-requeue selections always commit",
          "[player][selection]") {
  CommitSelectionContext c;
  c.is_requeue = false;
  c.has_selection = true;
  c.current_positions_sufficient = true;
  c.selected_positions_sufficient = true;
  c.action_type = e_FunctionType_Movement;
  c.command_type = e_FunctionType_Movement;
  c.current_outgoing_velocity = e_Velocity_Walk;
  c.current_quadrant = 2;
  c.selected_quadrant = 2;
  CHECK(CommitActionSelectionAllowed(c));
}

TEST_CASE("Commit gate 6b: missing selection or trajectory commits",
          "[player][selection]") {
  CommitSelectionContext c;
  c.is_requeue = true;
  c.has_selection = false;
  CHECK(CommitActionSelectionAllowed(c));
  c.has_selection = true;
  c.current_positions_sufficient = false;
  CHECK(CommitActionSelectionAllowed(c));
}

TEST_CASE("A7: passive trip is rejected while special states are active",
          "[player][selection]") {
  football::sim::PassiveTripContext c;
  c.action_type = e_FunctionType_Movement;
  c.interrupt_is_none = true;
  c.requested_trip_type = 1;
  CHECK(football::sim::CanAcceptPassiveTrip(c));

  c.incoming_special_state_empty = false;
  CHECK_FALSE(football::sim::CanAcceptPassiveTrip(c));
  c.incoming_special_state_empty = true;
  c.outgoing_special_state_empty = false;
  CHECK_FALSE(football::sim::CanAcceptPassiveTrip(c));
}

TEST_CASE("A7: passive trip is rejected while an interrupt is pending",
          "[player][selection]") {
  football::sim::PassiveTripContext c;
  c.action_type = e_FunctionType_Movement;
  c.requested_trip_type = 1;
  c.interrupt_is_none = false;
  CHECK_FALSE(football::sim::CanAcceptPassiveTrip(c));
}

TEST_CASE("A7: sliding cannot be interrupted by a passive trip",
          "[player][selection]") {
  football::sim::PassiveTripContext c;
  c.action_type = e_FunctionType_Sliding;
  c.interrupt_is_none = true;
  c.requested_trip_type = 3;
  CHECK_FALSE(football::sim::CanAcceptPassiveTrip(c));
}

TEST_CASE("A7: only a harder trip may replace a pass-through trip",
          "[player][selection]") {
  football::sim::PassiveTripContext c;
  c.action_type = e_FunctionType_Trip;
  c.interrupt_is_none = true;
  c.action_trip_type = 1;   // pass-through
  c.requested_trip_type = 1;  // same type -> rejected
  CHECK_FALSE(football::sim::CanAcceptPassiveTrip(c));
  c.requested_trip_type = 2;  // harder -> accepted
  CHECK(football::sim::CanAcceptPassiveTrip(c));
  c.action_trip_type = 0;  // a real fall, not pass-through
  CHECK_FALSE(football::sim::CanAcceptPassiveTrip(c));
}
