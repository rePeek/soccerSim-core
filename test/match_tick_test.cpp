#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match/match.hpp"
#include "sim/simulation.hpp"
#include "sim/time/tick.hpp"
#include "sim/time/tick_boundary.hpp"
#include "sim/testing/simulation_access.hpp"
using football::sim::testing::SimulationAccess;

namespace {
using namespace football::sim;

void Init(Simulation& simulation, MatchOptions options = {}) {
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                  football::app::fixtures::MakeDefaultAwayTeam(),
                  football::model::MakeLegacyPitch(), options);
}

TEST_CASE("Match snapshots read the authoritative tick timeline and freeze at full time",
          "[sim][tick]") {
  for (const bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.half_duration = TickSpan{1};
    Simulation simulation;
    Init(simulation, options);
    REQUIRE(simulation.match()->GetTimelineTick() == Tick{});
    const auto policy = football::test::MakeDefaultAI(simulation);
    std::uint64_t steps = 0;
    while (!simulation.Finished()) {
      const Tick before = simulation.match()->GetTimelineTick();
      football::test::StepDefaultAI(simulation, policy);
      REQUIRE(++steps < 3000);
      const Tick now = simulation.match()->GetTimelineTick();
      REQUIRE(now >= before);
      REQUIRE(simulation.Observe().tick == now.value);
    }
    REQUIRE(simulation.Observe().regulation_time == TickSpan{2});
    REQUIRE(simulation.Result().duration_ticks == steps);
    const Tick finished = simulation.match()->GetTimelineTick();
    const auto rng = simulation.match()->rng().engine();
    for (int i = 0; i < 20; ++i) simulation.Step({});
    REQUIRE(simulation.match()->GetTimelineTick() == finished);
    REQUIRE(simulation.Result().duration_ticks == steps);
    REQUIRE(simulation.match()->rng().engine() == rng);
    simulation.Stop();
    Init(simulation, options);
    REQUIRE(simulation.match()->GetTimelineTick() == Tick{});
  }
}

TEST_CASE("Advancing timeline ticks does not execute physics or consume RNG",
          "[sim][tick]") {
  Simulation simulation;
  Init(simulation);
  Match* match = simulation.match();
  const auto before = simulation.Observe();
  const auto rng = match->rng().engine();
  simulation.AdvanceTime(Seconds(2));
  REQUIRE(match->GetTimelineTick() == Tick{200});
  simulation.AdvanceTime(Seconds(1));
  REQUIRE(match->GetTimelineTick() == Tick{300});
  const auto after = simulation.Observe();
  REQUIRE(after.tick == 300);
  REQUIRE(after.regulation_time == before.regulation_time);
  REQUIRE(after.ball_in_play_time == before.ball_in_play_time);
  REQUIRE(after.phase == before.phase);
  REQUIRE(after.reset_sequence == before.reset_sequence);
  REQUIRE(after.ball_position == before.ball_position);
  REQUIRE(after.ball_velocity == before.ball_velocity);
  REQUIRE(after.players.size() == before.players.size());
  for (std::size_t i = 0; i < after.players.size(); ++i) {
    REQUIRE(after.players[i].position == before.players[i].position);
    REQUIRE(after.players[i].velocity == before.players[i].velocity);
    REQUIRE(after.players[i].facing == before.players[i].facing);
  }
  REQUIRE(match->rng().engine() == rng);
  REQUIRE_THROWS_AS(simulation.AdvanceTime(TickSpan{std::numeric_limits<std::uint64_t>::max()}),
                    std::overflow_error);
  REQUIRE(match->GetTimelineTick() == Tick{300});
  REQUIRE(simulation.Observe().regulation_time == before.regulation_time);
  REQUIRE(simulation.Observe().ball_in_play_time == before.ball_in_play_time);
  REQUIRE(match->rng().engine() == rng);
}

TEST_CASE("Tick timeline executes ordinary restart positioning without a skip",
          "[sim][tick][restart]") {
  for (const bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.half_duration = Seconds(18);
    Simulation simulation;
    Init(simulation, options);
    football::test::TakeKickOff(simulation);
    // Let the original post-kickoff relaxation expire before forcing a throw-in.
    for (int i = 0; i < 40; ++i) simulation.Step({});
    Match* match = simulation.match();
    REQUIRE(match->IsInPlay());
    REQUIRE_FALSE(match->IsInSetPiece());
    match->GetBall()->ResetSituation(blunted::Vector3(10.f, 40.f, 0.f));
    const Tick before = match->GetTimelineTick();
    const auto football_clock = match->GetRegulationTime();
    const auto in_play_clock = match->GetBallInPlayTime();
    const auto resets = match->GetResetSequence();
    simulation.Step({});
    REQUIRE(simulation.Observe().restart == e_GameMode_ThrowIn);
    REQUIRE(match->GetTimelineTick() == before + TickSpan{1});
    const auto scheduled = match->GetReferee()->GetBuffer();
    REQUIRE(scheduled.restart.has_value());
    REQUIRE(scheduled.restart->entered_tick == before);
    REQUIRE(scheduled.restart->earliest_restart_tick == before + Seconds(2));
    REQUIRE(match->GetRegulationTime() == football_clock + TickSpan{1});
    REQUIRE(match->GetBallInPlayTime() == in_play_clock);
    REQUIRE(match->GetResetSequence() == resets);
    simulation.Step({});
    REQUIRE(match->GetTimelineTick() == before + TickSpan{2});
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE(match->GetReferee()->GetBuffer().taker != nullptr);
    REQUIRE(match->GetRegulationTime() == football_clock + TickSpan{2});
    REQUIRE(match->GetBallInPlayTime() == in_play_clock);
    REQUIRE(simulation.Observe().restart_pending);
    for (int i = 0; i < 210; ++i) simulation.Step({});
    REQUIRE_FALSE(match->IsInPlay()); // A minimum is not automatic permission.
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE(match->GetRegulationTime() == football_clock + TickSpan{212});
    REQUIRE(match->GetBallInPlayTime() == in_play_clock);
  }
}

TEST_CASE("Restart setup and timeout fire once across timeline jumps",
          "[sim][tick][referee][restart]") {
  for (const bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.half_duration = Minutes(3);
    Simulation simulation;
    Init(simulation, options);
    football::test::TakeKickOff(simulation);
    for (int i = 0; i < 40; ++i) simulation.Step({});
    Match* match = simulation.match();
    match->GetBall()->ResetSituation(blunted::Vector3(10.f, 40.f, 0.f));
    simulation.Step({});
    Referee* rules = match->GetReferee();
    const auto process_rules = [&] {
      simulation.Mirror(reverse, !reverse, reverse);
      SimulationAccess::ProcessRules(simulation, *rules);
      simulation.Mirror(reverse, !reverse, reverse);
    };
    const auto scheduled = rules->GetBuffer();
    const auto resets = match->GetResetSequence();
    REQUIRE(scheduled.taker == nullptr);
    simulation.AdvanceTime(Seconds(3));
    process_rules();
    REQUIRE(rules->GetBuffer().taker != nullptr);
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE_FALSE(match->IsInPlay());
    auto rng = match->rng().engine();
    process_rules();
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE(match->rng().engine() == rng);
    const auto timeout = scheduled.restart->timeout_tick;
    simulation.AdvanceTime((timeout - match->GetTimelineTick()) + TickSpan{3});
    process_rules();
    REQUIRE(match->IsInPlay());
    REQUIRE(match->IsInSetPiece());
    REQUIRE(rules->GetBuffer().restart->used_timeout_placement);
    REQUIRE(rules->GetBuffer().restart->phase == RestartPhase::Ready);
    rng = match->rng().engine();
    process_rules();
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE(match->rng().engine() == rng);
  }
}

TEST_CASE("Player touch and card effect timestamps use the timeline tick", "[sim][tick][player]") {
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    Simulation simulation;
    Init(simulation, options);
    while (!simulation.IsInPlay()) simulation.Step({});
    Match* match = simulation.match();
    auto* player = match->GetTeam(0)->GetAllPlayers()[1];
    const auto now = match->GetTimelineTick();
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    touch_sink.OnBallTouched({now, player, match->GetTeam(0), e_TouchType_Intentional_Kicked});
    REQUIRE(player->GetLastTouchTick() == now);
    const auto effective = now + TickSpan{5};
    player->GiveRedCard(effective);
    for (int i = 0; i < 5; ++i) {
      REQUIRE(player->IsActive());
      simulation.Step({});
    }
    REQUIRE(match->GetTimelineTick() == effective);
    REQUIRE(player->IsActive()); // Effect is checked at the start of Player::Process.
    simulation.Step({});
    REQUIRE_FALSE(player->IsActive());
    REQUIRE(match->GetTeam(0)->GetActivePlayersCount() == 10);
  }
}

template<class T> concept HasClockComposition = requires(T& owner) { owner.AdvanceTime(TickSpan{1}); };
static_assert(!HasClockComposition<Match>);

TEST_CASE("diagnostic clock advancement rejects a stopped Simulation", "[sim][tick]") {
  Simulation simulation;
  REQUIRE_THROWS_AS(simulation.AdvanceTime(TickSpan{1}), std::logic_error);
  Init(simulation);
  simulation.Stop();
  REQUIRE_THROWS_AS(simulation.AdvanceTime(TickSpan{1}), std::logic_error);
}

TEST_CASE("Simulation updates the recent possession window only after admitted open-play ticks",
          "[sim][tick][possession]") {
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.half_duration = Seconds(100);
    Simulation simulation;
    Init(simulation, options);
    const auto policy = football::test::MakeDefaultAI(simulation);
    for (int attempts = 0; !simulation.Observe().ball_in_play; ++attempts) {
      REQUIRE(attempts < 1000);
      football::test::StepDefaultAI(simulation, policy);
    }
    auto& match = *simulation.match();
    REQUIRE(match.IsInSetPiece());
    REQUIRE(match.GetPossessionFactor_60seconds() == 0.f);
    simulation.AdvanceTime(Seconds(2));
    REQUIRE(match.GetPossessionFactor_60seconds() == 0.f);
    simulation.Step({}); // Referee releases Taken before this tick's window update.
    REQUIRE_FALSE(match.IsInSetPiece());
    const bool home = match.GetDesignatedPossessionPlayer()->GetTeam() == match.GetTeam(0);
    REQUIRE(match.GetPossessionFactor_60seconds() == (home ? -0.01f : 0.01f) / 60.f);
    const auto rng = match.rng().engine();
    const auto ball = match.GetBall()->Predict(TickSpan{});
    simulation.AdvanceTime(Seconds(61));
    const float saturated = home ? -1.f : 1.f;
    REQUIRE(match.GetPossessionFactor_60seconds() == saturated);
    REQUIRE(match.GetBall()->Predict(TickSpan{}) == ball);
    REQUIRE(match.rng().engine() == rng);
    REQUIRE_THROWS_AS(simulation.AdvanceTime(TickSpan{std::numeric_limits<std::uint64_t>::max()}),
                      std::overflow_error);
    REQUIRE(match.GetPossessionFactor_60seconds() == saturated);
    match.StopPlay();
    const auto effective = match.GetBallInPlayTime();
    simulation.AdvanceTime(Seconds(100)); // Regulation clips; dead balls do not update the window.
    REQUIRE(match.GetRegulationTime() == options.half_duration);
    REQUIRE(match.GetBallInPlayTime() == effective);
    REQUIRE(match.GetPossessionFactor_60seconds() == saturated);
    match.StartPlay();
    match.StartBallInPlay();
    simulation.AdvanceTime(Seconds(1)); // No admitted ticks remain in this period.
    REQUIRE(match.GetPossessionFactor_60seconds() == saturated);
    REQUIRE(match.rng().engine() == rng);
  }
}

}  // namespace
