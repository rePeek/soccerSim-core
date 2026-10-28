// ANIM-1.1 — action request / reaction arbitration (pure).

#include <catch2/catch_test_macros.hpp>

#include "football/foundation/time/tick.hpp"
#include "player/player_action_request.hpp"

using football::sim::TickSpan;

namespace {
PlayerActionState Action(e_FunctionType type, std::uint64_t duration,
                         std::uint64_t elapsed) {
  PlayerActionState state;
  state.type = type;
  state.duration = TickSpan{duration};
  state.elapsed = TickSpan{elapsed};
  return state;
}
}  // namespace

TEST_CASE("a request while moving is accepted, and None is rejected") {
  const PlayerActionState moving = Action(e_FunctionType_Movement, 20, 5);
  CHECK(ArbitrateActionRequest(moving, ControlAction::Shoot) ==
        RequestOutcome::Accepted);
  CHECK(ArbitrateActionRequest(moving, ControlAction::None) ==
        RequestOutcome::Rejected);
}

TEST_CASE("a one-shot in flight is not restarted by repeated input") {
  const PlayerActionState shooting = Action(e_FunctionType_Shot, 20, 3);
  // Same action again: rejected, so repeated control input cannot restart it.
  CHECK(ArbitrateActionRequest(shooting, ControlAction::Shoot) ==
        RequestOutcome::Rejected);
  // A different one-shot: deferred until a legal switch window.
  CHECK(ArbitrateActionRequest(shooting, ControlAction::ShortPass) ==
        RequestOutcome::Deferred);
}

TEST_CASE("a completed one-shot accepts the next request") {
  const PlayerActionState done = Action(e_FunctionType_Shot, 20, 20);
  CHECK(ArbitrateActionRequest(done, ControlAction::ShortPass) ==
        RequestOutcome::Accepted);
  CHECK(ArbitrateActionRequest(done, ControlAction::None) ==
        RequestOutcome::Rejected);
}

TEST_CASE("a physics reaction always preempts") {
  const PlayerActionState shooting = Action(e_FunctionType_Shot, 20, 3);
  CHECK(ArbitrateReaction(shooting, ReactionKind::Trip) ==
        RequestOutcome::Accepted);
  CHECK(ArbitrateReaction(shooting, ReactionKind::Bump) ==
        RequestOutcome::Accepted);
  CHECK(ArbitrateReaction(shooting, ReactionKind::None) ==
        RequestOutcome::Rejected);
}

TEST_CASE("action kinds map to the legacy function types") {
  CHECK(ActionRequestFunctionType(ControlAction::None) ==
        e_FunctionType_None);
  CHECK(ActionRequestFunctionType(ControlAction::Shoot) ==
        e_FunctionType_Shot);
  CHECK(ActionRequestFunctionType(ControlAction::ShortPass) ==
        e_FunctionType_ShortPass);
  CHECK(ActionRequestFunctionType(ControlAction::ThroughPass) ==
        e_FunctionType_ShortPass);
  CHECK(ActionRequestFunctionType(ControlAction::Beat) ==
        e_FunctionType_BallControl);
  // Tackle is a standing challenge; SlideTackle is the sliding one.
  CHECK(ActionRequestFunctionType(ControlAction::Tackle) ==
        e_FunctionType_Interfere);
  CHECK(ActionRequestFunctionType(ControlAction::SlideTackle) ==
        e_FunctionType_Sliding);
  CHECK(ActionRequestFunctionType(ControlAction::Pull) ==
        e_FunctionType_None);
  CHECK(ActionRequestFunctionType(ControlAction::CallSupport) ==
        e_FunctionType_None);
  CHECK(ActionRequestFunctionType(ControlAction::Save) ==
        e_FunctionType_Deflect);
  CHECK(ActionRequestFunctionType(ControlAction::None) ==
        e_FunctionType_None);
}
