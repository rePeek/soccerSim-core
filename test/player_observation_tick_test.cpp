#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/query/reachability.hpp"

namespace {
using namespace football::sim;

TEST_CASE("Continuous reachability estimates retain sub-tick ranking precision", "[sim][tick][reachability]") {
  struct Case { float metres; unsigned usual_ms; unsigned optimistic_ms; };
  const Case cases[] = {{0.02f, 1, 1}, {2.84f, 703, 600},
                        {8.815f, 1551, 1463}, {20.f, 3556, 3356}};
  for (const auto& sample : cases) {
    const auto estimate = query::GetTimeNeededForDistance_ms(
        blunted::Vector3(0), blunted::Vector3(0), blunted::Vector3(sample.metres, 0, 0), 7.5f);
    REQUIRE(estimate.usual_ms == sample.usual_ms);
    REQUIRE(estimate.optimistic_ms == sample.optimistic_ms);
    REQUIRE(estimate.usual_ms % 10 != 0); // Not an integer-grid deadline.
  }
  const auto near = query::GetTimeNeededForDistance_ms(
      blunted::Vector3(0), blunted::Vector3(0), blunted::Vector3(0.02f, 0, 0), 7.5f);
  const auto further = query::GetTimeNeededForDistance_ms(
      blunted::Vector3(0), blunted::Vector3(0), blunted::Vector3(0.06f, 0, 0), 7.5f);
  REQUIRE(near.usual_ms < further.usual_ms);
  REQUIRE(near.usual_ms / 10 == further.usual_ms / 10); // Flooring would invent a tie.
}

void Init(Simulation& simulation, bool reverse) {
  MatchOptions options;
  options.reverse_team_processing = reverse;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
}

template<class T> concept HasUnusedQueueShadow = requires(T& actor) {
  actor.HasSimulationDecisionQueue();
};
template<class T> concept HasMillisecondPublication = requires(T& actor) {
  actor.GetLastDirectMovementIntentPublication_ms();
};
static_assert(!HasUnusedQueueShadow<Player>);
static_assert(!HasMillisecondPublication<Player>);
static_assert(std::is_same_v<decltype(LocomotionReentryAudit::decision_age_sum), TickSpan>);
static_assert(std::is_same_v<decltype(LocomotionReentryAudit::decision_age_max), std::optional<TickSpan>>);

TEST_CASE("Player publication and reset stamps distinguish absent from tick zero", "[sim][tick][player]") {
  for (bool reverse : {false, true}) {
    Simulation simulation; Init(simulation, reverse);
    auto& match = *simulation.match();
    auto& actor = *match.GetTeam(0)->GetAllPlayers()[1];
    REQUIRE_FALSE(actor.GetLastDecisionLocomotionPublicationTick());
    REQUIRE_FALSE(actor.GetLastResetSituationTick());
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    const auto rng = match.rng().engine();
    actor.PublishDecisionLocomotionIntent(command);
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{});
    REQUIRE(match.rng().engine() == rng);
    actor.ResetSituation(actor.GetPosition());
    REQUIRE(actor.GetLastResetSituationTick() == Tick{});
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{});
    REQUIRE(actor.DecisionLocomotionEpochIsStale()); // Same-tick reset still breaks continuity.

    // Beyond the former signed-int millisecond range, without invoking physics.
    const TickSpan large{UINT64_C(1) << 32};
    match.AdvanceTime(large);
    actor.PublishDecisionLocomotionIntent(command);
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{large.value});
    REQUIRE_FALSE(actor.DecisionLocomotionEpochIsStale());
    match.AdvanceTime(TickSpan{3});
    actor.ResetSituation(actor.GetPosition());
    REQUIRE(actor.GetLastResetSituationTick() == Tick{large.value + 3});
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{large.value});
    REQUIRE(actor.DecisionLocomotionEpochIsStale());

    ResetLocomotionReentryAudits();
    actor.NoteLocomotionReentryTick(true, false, match.GetTimelineTick()); // Epoch entry.
    actor.NoteLocomotionReentryTick(true, false, match.GetTimelineTick());
    const auto& audit = LocomotionReentryAuditFor(1);
    REQUIRE(audit.stale_after_reset == 1);
    REQUIRE(audit.decision_age_sum == TickSpan{3});
    REQUIRE(audit.decision_age_max == TickSpan{3});
    REQUIRE(audit.decision_age_count == 1);
    actor.NoteLocomotionReentryTick(true, false, Tick{large.value - 1});
    REQUIRE(LocomotionNegativeDecisionAgeSamples() == 1);
    REQUIRE(audit.decision_age_sum == TickSpan{3}); // Rewinds are measured, never unsigned-wrapped.
  }
}

TEST_CASE("Mental history tick sampling preserves nearest-capture reaction boundaries", "[sim][tick][perception]") {
  for (bool reverse : {false, true}) {
    Simulation simulation; Init(simulation, reverse);
    auto& match = *simulation.match();
    REQUIRE_THROWS_AS(match.GetMentalImage(TickSpan{}), std::logic_error);
    REQUIRE_THROWS_AS(match.GetMentalImage(0), std::logic_error);
    football::test::TakeKickOff(simulation);
    const auto rng = match.rng().engine();
    const auto captured = match.GetMentalImage(TickSpan{})->captured_tick;
    for (std::uint64_t ticks = 0; ticks < 400; ++ticks) {
      const auto slot = std::min<std::uint64_t>((ticks + 5) / 10, 2);
      REQUIRE(match.GetMentalImage(TickSpan{ticks})->captured_tick == captured - TickSpan{slot * 10});
      REQUIRE(match.GetMentalImage(TickSpan{ticks}) == match.GetMentalImage(static_cast<int>(ticks * 10)));
    }
    // Sub-tick reaction estimates must not acquire new ties or new snapshots.
    // Only the existing 100 ms capture-slot sampling is rounded here.
    for (int ms = -200; ms <= 400; ++ms) {
      const auto slot = std::clamp(int(std::round(float(ms) / 100.0)), 0, 2);
      REQUIRE(match.GetMentalImage(ms)->captured_tick == captured - TickSpan{std::uint64_t(slot * 10)});
    }
    REQUIRE(match.GetMentalImage(49) == match.GetMentalImage(TickSpan{}));
    REQUIRE(match.GetMentalImage(50) == match.GetMentalImage(TickSpan{10}));
    REQUIRE(match.GetMentalImage(149) == match.GetMentalImage(TickSpan{10}));
    REQUIRE(match.GetMentalImage(150) == match.GetMentalImage(TickSpan{20}));
    REQUIRE(match.GetMentalImage(std::numeric_limits<int>::min()) == match.GetMentalImage(TickSpan{}));
    REQUIRE(match.GetMentalImage(std::numeric_limits<int>::max()) == match.GetMentalImage(TickSpan{20}));
    REQUIRE(match.GetMentalImage(TickSpan{std::numeric_limits<std::uint64_t>::max()}) ==
            match.GetMentalImage(TickSpan{20}));
    REQUIRE(match.rng().engine() == rng);
    match.ResetSituation(blunted::Vector3(0));
    REQUIRE_THROWS_AS(match.GetMentalImage(TickSpan{}), std::logic_error);
    simulation.Step({});
    REQUIRE(match.GetMentalImage(TickSpan{100}) == match.GetMentalImage(TickSpan{}));
  }
}
} // namespace
