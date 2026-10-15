// ANIM-1 A6.3c.0 — action cursor semantics audit.
//
// Documents the boundary between the legacy `currentAnim.frameNum++` projection
// and the authoritative PlayerActionState cursor before the A6.3c clock flip.

#include <catch2/catch_test_macros.hpp>

#include "sim/player/player_action_executor.hpp"

namespace {
PlayerActionDefinition MovementDefinition(std::uint64_t frames) {
  PlayerActionDefinition definition;
  definition.type = e_FunctionType_Movement;
  definition.duration = football::sim::TickSpan{frames};
  return definition;
}
}  // namespace

TEST_CASE("the action cursor saturates at the duration and never wraps") {
  PlayerActionState state;
  PlayerActionExecutor::Begin(state, MovementDefinition(25));
  CHECK(state.Frame() == 0);
  CHECK_FALSE(state.IsComplete());

  // A single oversized advance saturates, matching the bounded legacy install
  // path rather than wrapping the integer cursor.
  PlayerActionExecutor::Step(state, football::sim::TickSpan{1000});
  CHECK(state.Frame() == 25);
  CHECK(state.elapsed == state.duration);
  CHECK(state.IsComplete());

  // Further ticks stay pinned at the last frame.
  PlayerActionExecutor::Step(state, football::sim::TickSpan{1});
  CHECK(state.Frame() == 25);
}

TEST_CASE("a non-zero start frame seeds the cursor exactly once") {
  PlayerActionState state;
  PlayerActionExecutor::Begin(state, MovementDefinition(25));
  // ResetPosition can deliberately start an idle clip at a random non-zero
  // frame; that initial cursor is established once.
  PlayerActionExecutor::Step(state, football::sim::TickSpan{7});
  CHECK(state.Frame() == 7);
  PlayerActionExecutor::Step(state, football::sim::TickSpan{1});
  CHECK(state.Frame() == 8);
}

TEST_CASE("a scheduled contact fires on exactly one tick") {
  PlayerActionDefinition definition = MovementDefinition(20);
  definition.type = e_FunctionType_Trap;
  definition.contact = football::sim::TickSpan{5};
  PlayerActionState state;
  PlayerActionExecutor::Begin(state, definition);

  int triggers = 0;
  for (int tick = 0; tick < 20; ++tick) {
    if (PlayerActionExecutor::Step(state, football::sim::TickSpan{1}).contactTriggered) {
      ++triggers;
    }
  }
  CHECK(triggers == 1);
  CHECK(state.Frame() == 20);
}

TEST_CASE("an action completes exactly once at the final frame") {
  PlayerActionState state;
  PlayerActionExecutor::Begin(state, MovementDefinition(10));
  int completions = 0;
  for (int tick = 0; tick < 30; ++tick) {
    if (PlayerActionExecutor::Step(state, football::sim::TickSpan{1}).completed) {
      ++completions;
    }
  }
  CHECK(completions == 1);
}
