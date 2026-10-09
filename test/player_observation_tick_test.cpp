#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "default_ai_fixture.hpp"
#include "sim/query/reachability.hpp"

namespace {
using namespace football::sim;
using football::sim::testing::SimulationAccess;

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
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, options);
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
    auto& actor = *SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
    REQUIRE_FALSE(actor.GetLastDecisionLocomotionPublicationTick());
    REQUIRE_FALSE(actor.GetLastResetSituationTick());
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    actor.PublishDecisionLocomotionIntent(command, SimulationAccess::NowOf(simulation), SimulationAccess::BallRetainerOf(simulation) == &actor);
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{});
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
    actor.ResetSituation(actor.GetPosition(), SimulationAccess::NowOf(simulation));
    REQUIRE(actor.GetLastResetSituationTick() == Tick{});
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{});
    REQUIRE(actor.DecisionLocomotionEpochIsStale()); // Same-tick reset still breaks continuity.

    // Beyond the former signed-int millisecond range, without invoking physics.
    const TickSpan large{UINT64_C(1) << 32};
    simulation.AdvanceTime(large);
    actor.PublishDecisionLocomotionIntent(command, SimulationAccess::NowOf(simulation), SimulationAccess::BallRetainerOf(simulation) == &actor);
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{large.value});
    REQUIRE_FALSE(actor.DecisionLocomotionEpochIsStale());
    simulation.AdvanceTime(TickSpan{3});
    actor.ResetSituation(actor.GetPosition(), SimulationAccess::NowOf(simulation));
    REQUIRE(actor.GetLastResetSituationTick() == Tick{large.value + 3});
    REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{large.value});
    REQUIRE(actor.DecisionLocomotionEpochIsStale());

    ResetLocomotionReentryAudits();
    actor.NoteLocomotionReentryTick(true, false, SimulationAccess::NowOf(simulation)); // Epoch entry.
    actor.NoteLocomotionReentryTick(true, false, SimulationAccess::NowOf(simulation));
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
  using std::chrono::milliseconds;
  for (bool reverse : {false, true}) {
    Simulation simulation; Init(simulation, reverse);
    REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
    REQUIRE_THROWS_AS(simulation.GetMentalImage(milliseconds{0}), std::logic_error);
    football::test::TakeKickOff(simulation);
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    const auto captured = simulation.GetMentalImage(TickSpan{})->captured_tick;
    for (std::uint64_t ticks = 0; ticks < 400; ++ticks) {
      const auto slot = std::min<std::uint64_t>((ticks + 5) / 10, 2);
      REQUIRE(simulation.GetMentalImage(TickSpan{ticks})->captured_tick == captured - TickSpan{slot * 10});
      REQUIRE(simulation.GetMentalImage(TickSpan{ticks}) == simulation.GetMentalImage(milliseconds{ticks * 10}));
    }
    // Sub-tick reaction estimates must not acquire new ties or new snapshots.
    // Only the existing 100 ms capture-slot sampling is rounded here.
    for (int ms = -200; ms <= 400; ++ms) {
      const auto slot = std::clamp(int(std::round(float(ms) / 100.0)), 0, 2);
      REQUIRE(simulation.GetMentalImage(milliseconds{ms})->captured_tick == captured - TickSpan{std::uint64_t(slot * 10)});
    }
    REQUIRE(simulation.GetMentalImage(milliseconds{49}) == simulation.GetMentalImage(TickSpan{}));
    REQUIRE(simulation.GetMentalImage(milliseconds{50}) == simulation.GetMentalImage(TickSpan{10}));
    REQUIRE(simulation.GetMentalImage(milliseconds{149}) == simulation.GetMentalImage(TickSpan{10}));
    REQUIRE(simulation.GetMentalImage(milliseconds{150}) == simulation.GetMentalImage(TickSpan{20}));
    REQUIRE(simulation.GetMentalImage(milliseconds{std::numeric_limits<int>::min()}) == simulation.GetMentalImage(TickSpan{}));
    REQUIRE(simulation.GetMentalImage(milliseconds{std::numeric_limits<int>::max()}) == simulation.GetMentalImage(TickSpan{20}));
    REQUIRE(simulation.GetMentalImage(milliseconds::min()) == simulation.GetMentalImage(TickSpan{}));
    REQUIRE(simulation.GetMentalImage(milliseconds::max()) == simulation.GetMentalImage(TickSpan{20}));
    REQUIRE(simulation.GetMentalImage(TickSpan{std::numeric_limits<std::uint64_t>::max()}) ==
            simulation.GetMentalImage(TickSpan{20}));
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
    simulation.ResetSituation(blunted::Vector3(0));
    REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
    simulation.Step({});
    REQUIRE(simulation.GetMentalImage(TickSpan{100}) == simulation.GetMentalImage(TickSpan{}));
  }
}

struct HumanoidHistoryTypeProbe : HumanoidBase { using HumanoidBase::mentalImageTime; };
static_assert(std::is_same_v<decltype(HumanoidHistoryTypeProbe::mentalImageTime), std::chrono::milliseconds>);
template<class T> concept HasUntypedHistory = requires(T& owner) { owner.GetMentalImage(0); };
template<class T> concept HasMatchHistory = requires(T& owner) { owner.GetMentalImage(TickSpan{}); };

template<class T> concept HasMillisecondTimeline = requires(T& owner) { owner.GetActualTime_ms(); };
template<class T> concept HasMillisecondAdvance = requires(T& owner) { owner.BumpActualTime_ms(10); };
template<class T> concept HasMillisecondTouch = requires(T& owner) { owner.GetLastTouchTime_ms(); };
static_assert(!HasMillisecondTouch<Player>);
template<class T> concept HasImplicitTouchBias = requires(T& actor) { actor.GetLastTouchBias(503); };
static_assert(!HasImplicitTouchBias<Player>);

TEST_CASE("Touch decay uses relative ticks without quantizing ability-dependent decay", "[sim][tick][player]") {
  for (bool reverse : {false, true}) {
    Simulation simulation; Init(simulation, reverse);
    auto& actor = *SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    actor.SetLastTouchTick(Tick{17});
    for (int decay_ms : {0, -1, 200, 240, 503, 707, 1000, 1199, 1500}) {
      for (std::uint64_t age = 0; age <= 200; ++age) {
        const float expected = decay_ms > 0
            ? 1.f - blunted::clamp(ToMilliseconds(TickSpan{age}) / float(decay_ms), 0.f, 1.f) : 0.f;
        REQUIRE(actor.GetLastTouchBias(decay_ms, Tick{17 + age}) == expected);
      }
    }
    actor.SetLastTouchTick(Tick{});
    simulation.AdvanceTime(TickSpan{23});
    REQUIRE(actor.GetLastTouchBias(503, SimulationAccess::NowOf(simulation)) == actor.GetLastTouchBias(503, Tick{23}));
    REQUIRE(actor.GetLastTouchBias(503, Tick{}) == 1.f); // Explicit zero is not omitted.
    actor.SetLastTouchTick(Tick{30});
    REQUIRE(actor.GetLastTouchBias(503, Tick{}) == 0.f); // Rewound observation.
    const TickSpan large{UINT64_C(1) << 63};
    simulation.AdvanceTime(large);
    actor.SetLastTouchTick(SimulationAccess::NowOf(simulation) - TickSpan{7});
    REQUIRE(actor.GetLastTouchBias(503, SimulationAccess::NowOf(simulation)) == 1.f - 70.f / 503.f);
    REQUIRE(actor.GetLastTouchBias(503, Tick{std::numeric_limits<std::uint64_t>::max()}) == 0.f);
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
  }
}

template<class T> concept HasImplicitLocomotionEligibility = requires(T& actor) { actor.IsEligibleForProceduralLocomotion(); };
template<class T> concept HasImplicitIntentRefresh = requires(T& actor) { actor.CommitLocomotionIntentRefresh(); };
static_assert(!HasImplicitLocomotionEligibility<Player>);
static_assert(!HasImplicitIntentRefresh<Player>);

TEST_CASE("Decision publications consume supplied time and retention facts", "[sim][player][dependency]") {
  Simulation simulation; Init(simulation, false);
  auto& actor = *SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Movement;
  command.useDesiredMovement = true;
  const auto rng = SimulationAccess::RngOf(simulation).engine();
  actor.PublishDecisionLocomotionIntent(command, Tick{123}, false);
  REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{123});
  REQUIRE(actor.IsEligibleForProceduralLocomotion(false));
  REQUIRE_FALSE(actor.IsEligibleForProceduralLocomotion(true));
  actor.CommitLocomotionIntentRefresh(*SimulationAccess::BallOf(simulation), Tick{123}, false);
  REQUIRE_FALSE(actor.IsLocomotionIntentRefreshDue(Tick{123}));
  REQUIRE(SimulationAccess::NowOf(simulation) == Tick{});
  REQUIRE(SimulationAccess::BallRetainerOf(simulation) == nullptr);
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
}

template<class T> concept HasImplicitDeactivation = requires(T& actor) { actor.Deactivate(); };
template<class T> concept HasImplicitSendOff = requires(T& actor) { actor.SendOff(); };
template<class T> concept HasImplicitResetTime = requires(T& actor) { actor.ResetSituation(Vector3(0)); };
static_assert(!HasImplicitDeactivation<Player>);
static_assert(!HasImplicitSendOff<Player>);
static_assert(!HasImplicitResetTime<Player>);

TEST_CASE("Player reset provenance consumes the supplied tick", "[sim][player][dependency]") {
  Simulation simulation; Init(simulation, false);
  auto& actor = *SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  const auto epoch = actor.GetDecisionLocomotionContinuityEpoch();
  actor.ResetSituation(actor.GetPosition(), Tick{77});
  REQUIRE(actor.GetLastResetSituationTick() == Tick{77});
  REQUIRE(actor.GetDecisionLocomotionContinuityEpoch() == epoch + 1);
  REQUIRE(SimulationAccess::NowOf(simulation) == Tick{});
}
} // namespace
