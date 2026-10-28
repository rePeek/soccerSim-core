// CMD-1/CMD-2: a Command says what to do; Animation::Match decides how. The
// mapping is deliberately not one-to-one, and continuous movement is not a
// discrete action request.

#include <catch2/catch_test_macros.hpp>

#include "football/player/player_command_validation.hpp"
#include "player/execution/player_action_mapping.hpp"
#include "player/execution/player_action_arbitration.hpp"

using football::math::Vector3;
using football::sim::player::ControlActionFunctionType;
using football::sim::player::ControlActionRequiresAim;
using football::sim::player::ControlActionRequiresExplicitTarget;
using football::sim::player::IsPassControlAction;

TEST_CASE("Command verbs map to execution primitives, not one animation each") {
  CHECK(ControlActionFunctionType(ControlAction::Pass) ==
        e_FunctionType_ShortPass);
  CHECK(ControlActionFunctionType(ControlAction::ShortPass) ==
        e_FunctionType_ShortPass);
  CHECK(ControlActionFunctionType(ControlAction::LongPass) ==
        e_FunctionType_LongPass);
  CHECK(ControlActionFunctionType(ControlAction::HighPass) ==
        e_FunctionType_HighPass);

  // ThroughPass is a pass into space: the concrete animation depends on the
  // explicit target distance, and no ThroughPass animation type exists.
  CHECK(ControlActionFunctionType(ControlAction::ThroughPass, 10.0f) ==
        e_FunctionType_ShortPass);
  CHECK(ControlActionFunctionType(ControlAction::ThroughPass, 45.0f) ==
        e_FunctionType_LongPass);
  CHECK(ControlActionFunctionType(ControlAction::ThroughPass) ==
        e_FunctionType_ShortPass);

  CHECK(ControlActionFunctionType(ControlAction::Shoot) ==
        e_FunctionType_Shot);
  CHECK(ControlActionFunctionType(ControlAction::Beat) ==
        e_FunctionType_BallControl);
  CHECK(ControlActionFunctionType(ControlAction::Tackle) ==
        e_FunctionType_Interfere);
  CHECK(ControlActionFunctionType(ControlAction::SlideTackle) ==
        e_FunctionType_Sliding);
  CHECK(ControlActionFunctionType(ControlAction::Pull) ==
        e_FunctionType_None);
  CHECK(ControlActionFunctionType(ControlAction::CallSupport) ==
        e_FunctionType_None);
  CHECK(ControlActionFunctionType(ControlAction::None) ==
        e_FunctionType_Movement);
}

TEST_CASE("the Command contract states which parameters are mandatory") {
  CHECK(ControlActionRequiresExplicitTarget(ControlAction::Pass));
  CHECK(ControlActionRequiresExplicitTarget(ControlAction::LongPass));
  CHECK(ControlActionRequiresExplicitTarget(ControlAction::ThroughPass));
  CHECK(ControlActionRequiresExplicitTarget(ControlAction::HighPass));
  CHECK_FALSE(ControlActionRequiresExplicitTarget(ControlAction::Shoot));
  CHECK(ControlActionRequiresAim(ControlAction::Shoot));
  CHECK_FALSE(ControlActionRequiresAim(ControlAction::Pass));
  CHECK_FALSE(ControlActionRequiresAim(ControlAction::Beat));
  CHECK(IsPassControlAction(ControlAction::ThroughPass));
  CHECK_FALSE(IsPassControlAction(ControlAction::Beat));
}

TEST_CASE("Play arbitration rejects Unsupported and duplicates") {
  PlayerActionState movement;
  movement.type = e_FunctionType_Movement;

  CHECK(ArbitrateActionRequest(movement, ControlAction::ThroughPass) ==
        RequestOutcome::Accepted);
  CHECK(ArbitrateActionRequest(movement, ControlAction::Beat) ==
        RequestOutcome::Accepted);
  CHECK(ArbitrateActionRequest(movement, ControlAction::SlideTackle) ==
        RequestOutcome::Accepted);
  CHECK(ArbitrateActionRequest(movement, ControlAction::Pull) ==
        RequestOutcome::Unsupported);
  CHECK(ArbitrateActionRequest(movement, ControlAction::CallSupport) ==
        RequestOutcome::Unsupported);
  CHECK(ArbitrateActionRequest(movement, ControlAction::None) ==
        RequestOutcome::Rejected);

  // An in-flight Beat is the ball-control family; a second Beat is rejected,
  // while a ThroughPass is a different family and waits for a switch window.
  PlayerActionState ball_control;
  ball_control.type = e_FunctionType_BallControl;
  ball_control.duration = football::sim::TickSpan{30};
  CHECK(ArbitrateActionRequest(ball_control, ControlAction::Beat) ==
        RequestOutcome::Rejected);
  CHECK(ArbitrateActionRequest(ball_control, ControlAction::ThroughPass) ==
        RequestOutcome::Deferred);
}

TEST_CASE("Command validation accepts only explicit pass and shot parameters") {
  PlayerControl pass;
  pass.action = ControlAction::Pass;
  CHECK_FALSE(football::sim::player::ControlActionTargetIsExplicit(pass));
  pass.target_player = 7;
  CHECK(football::sim::player::ControlActionTargetIsExplicit(pass));

  PlayerControl shot;
  shot.action = ControlAction::Shoot;
  CHECK_FALSE(football::sim::player::ControlActionAimIsExplicit(shot));
  shot.move_direction = Vector3(1, 0, 0);
  CHECK(football::sim::player::ControlActionAimIsExplicit(shot));

  PlayerControl aimed_shot;
  aimed_shot.action = ControlAction::Shoot;
  aimed_shot.target_position = Vector3(10, 5, 0);
  CHECK(football::sim::player::ControlActionAimIsExplicit(aimed_shot));
  const Vector3 direction = football::sim::player::ResolveExplicitShotDirection(
      aimed_shot, Vector3(0, 0, 0), Vector3(0, -1, 0));
  CHECK(direction.coords[0] > 0.8f);
}
