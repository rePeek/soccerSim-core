#include <limits>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "sim/player/player_action_executor.hpp"
#include "sim/player/locomotion_intent_scheduler.hpp"

namespace {
using football::sim::TickSpan;

template <typename T>
concept HasDuplicateActionTime = requires(T value) { value.elapsedTime_ms; } ||
    requires(T value) { value.durationTime_ms; } || requires(T value) { value.contactTime_ms; } ||
    requires(T value) { value.frame; } || requires(T value) { value.frameCount; };
static_assert(!HasDuplicateActionTime<PlayerActionState>);
static_assert(std::is_same_v<decltype(PlayerActionState::elapsed), TickSpan>);
static_assert(std::is_same_v<decltype(PlayerActionState::contact), std::optional<TickSpan>>);

TEST_CASE("Action ticks are the sole frame and contact authority", "[sim][tick][action]") {
  PlayerActionDefinition definition;
  definition.type = e_FunctionType_Shot;
  definition.duration = TickSpan{30};
  definition.contact = TickSpan{12};
  PlayerActionState state;
  PlayerActionExecutor::Begin(state, definition);
  REQUIRE(state.Frame() == 0);
  REQUIRE(state.FrameCount() == 30);
  REQUIRE(state.ContactFrame() == 12);
  auto result = PlayerActionExecutor::Step(state, TickSpan{10});
  REQUIRE(state.elapsed == TickSpan{10});
  REQUIRE(state.IsContactPending());
  REQUIRE_FALSE(result.contactTriggered);
  result = PlayerActionExecutor::Step(state, TickSpan{2});
  REQUIRE(state.Frame() == 12);
  REQUIRE(result.contactTriggered);
  REQUIRE_FALSE(result.completed);
  result = PlayerActionExecutor::Step(state, TickSpan{17});
  REQUIRE(state.IsAtLastFrame());
  REQUIRE_FALSE(state.IsComplete());
  REQUIRE_FALSE(result.contactTriggered);
  result = PlayerActionExecutor::Step(state, TickSpan{1});
  REQUIRE(state.IsComplete());
  REQUIRE(result.completed);
  result = PlayerActionExecutor::Step(state, TickSpan{1});
  REQUIRE(state.elapsed == TickSpan{30});
  REQUIRE_FALSE(result.completed);
}

TEST_CASE("Action time saturates safely and reset clears previous contact", "[sim][tick][action]") {
  PlayerActionDefinition definition;
  definition.type = e_FunctionType_ShortPass;
  definition.duration = TickSpan{30};
  definition.contact = TickSpan{12};
  PlayerActionState state;
  PlayerActionExecutor::Begin(state, definition);
  const auto result = PlayerActionExecutor::Step(state, TickSpan{std::numeric_limits<std::uint64_t>::max()});
  REQUIRE(state.elapsed == TickSpan{30});
  REQUIRE(result.contactTriggered);
  REQUIRE(result.completed);
  definition.type = e_FunctionType_Movement;
  definition.contact.reset();
  PlayerActionExecutor::Begin(state, definition);
  REQUIRE(state.elapsed == TickSpan{});
  REQUIRE_FALSE(state.HasScheduledContact());
  REQUIRE(state.ContactFrame() == -1); // Animation-index boundary sentinel, not a clock.
  REQUIRE(state.IsPureLocomotion(false));
  REQUIRE_FALSE(state.IsPureLocomotion(true));
}

TEST_CASE("A zero-tick contact is due at action entry, not emitted twice", "[sim][tick][action]") {
  PlayerActionDefinition definition;
  definition.duration = TickSpan{2};
  definition.contact = TickSpan{};
  PlayerActionState state;
  PlayerActionExecutor::Begin(state, definition);
  REQUIRE(state.HasScheduledContact());
  REQUIRE(state.IsContactDue());
  REQUIRE_FALSE(state.IsContactPending());
  REQUIRE(state.Frame() == state.ContactFrame());
  REQUIRE_FALSE(PlayerActionExecutor::Step(state, TickSpan{1}).contactTriggered);
}

TEST_CASE("Decision and locomotion schedulers use crossed tick cadences", "[sim][tick][scheduler]") {
  using football::sim::Tick;
  PlayerDecisionScheduler decisions;
  REQUIRE(decisions.Due(Tick{}, TickSpan{24}));
  decisions.Commit(Tick{100});
  REQUIRE_FALSE(decisions.Due(Tick{99}, TickSpan{2}));
  REQUIRE_FALSE(decisions.Due(Tick{101}, TickSpan{2}));
  REQUIRE(decisions.Due(Tick{102}, TickSpan{2}));
  REQUIRE(decisions.Due(Tick{120}, TickSpan{2}));
  REQUIRE_FALSE(decisions.Due(Tick{120}, TickSpan{24}));
  decisions.Commit(Tick{120});
  REQUIRE_FALSE(decisions.Due(Tick{120}, TickSpan{2}));
  LocomotionIntentScheduler locomotion;
  REQUIRE(locomotion.Due(Tick{}));
  locomotion.Schedule(Tick{100}, TickSpan{8});
  REQUIRE_FALSE(locomotion.Due(Tick{107}));
  REQUIRE(locomotion.Due(Tick{108}));
  REQUIRE(locomotion.Due(Tick{120}));
  REQUIRE(locomotion.refreshes == 1);
  const auto deadline = locomotion.next_refresh_tick;
  REQUIRE_THROWS_AS(locomotion.Schedule(
      Tick{std::numeric_limits<std::uint64_t>::max()}, TickSpan{1}), std::overflow_error);
  REQUIRE(locomotion.next_refresh_tick == deadline);
  REQUIRE(locomotion.refreshes == 1);
}

}  // namespace
