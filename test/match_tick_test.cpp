#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/match.hpp"
#include "sim/simulation.hpp"
#include "sim/tick.hpp"
#include "sim/tick_boundary.hpp"

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
    options.half_duration_ms = 1;  // Legacy football clock can still be sub-tick.
    Simulation simulation;
    Init(simulation, options);
    REQUIRE(simulation.match()->GetTimelineTick() == Tick{});
    std::uint64_t steps = 0;
    while (!simulation.Finished()) {
      const Tick before = simulation.match()->GetTimelineTick();
      simulation.Step({});
      REQUIRE(++steps < 1000);
      const Tick now = simulation.match()->GetTimelineTick();
      REQUIRE(now >= before);
      REQUIRE(simulation.Observe().tick == now.value);
      REQUIRE(simulation.match()->GetActualTime_ms() == ToMilliseconds(now));
    }
    REQUIRE(simulation.Observe().match_time_ms == 2);
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
  match->AdvanceTime(Seconds(2));
  REQUIRE(match->GetTimelineTick() == Tick{200});
  match->BumpActualTime_ms(1000);  // Temporary caller adapter uses the same storage.
  REQUIRE(match->GetTimelineTick() == Tick{300});
  REQUIRE(match->GetActualTime_ms() == 3000);
  const auto after = simulation.Observe();
  REQUIRE(after.tick == 300);
  REQUIRE(after.match_time_ms == before.match_time_ms);
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
  REQUIRE_THROWS_AS(match->BumpActualTime_ms(11), std::invalid_argument);
  REQUIRE_THROWS_AS(match->AdvanceTime(TickSpan{std::numeric_limits<std::uint64_t>::max()}),
                    std::overflow_error);
  REQUIRE(match->GetTimelineTick() == Tick{300});
  REQUIRE(simulation.Observe().match_time_ms == before.match_time_ms);
  REQUIRE(match->rng().engine() == rng);
}

TEST_CASE("Tick timeline executes ordinary restart positioning without a skip",
          "[sim][tick][restart]") {
  for (const bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.half_duration_ms = 18000;
    Simulation simulation;
    Init(simulation, options);
    int startup = 0;
    while (!simulation.IsInPlay()) {
      simulation.Step({});
      REQUIRE(++startup < 1000);
    }
    // Let the original post-kickoff relaxation expire before forcing a throw-in.
    for (int i = 0; i < 40; ++i) simulation.Step({});
    Match* match = simulation.match();
    REQUIRE(match->IsInPlay());
    REQUIRE_FALSE(match->IsInSetPiece());
    match->GetBall()->ResetSituation(blunted::Vector3(10.f, 40.f, 0.f));
    const Tick before = match->GetTimelineTick();
    const auto football_clock = match->GetMatchTime_ms();
    const auto resets = match->GetResetSequence();
    simulation.Step({});
    REQUIRE(simulation.Observe().restart == e_GameMode_ThrowIn);
    REQUIRE(match->GetTimelineTick() == before + TickSpan{1});
    const auto scheduled = match->GetReferee()->GetBuffer();
    REQUIRE(scheduled.restart.has_value());
    REQUIRE(scheduled.restart->entered_tick == before);
    REQUIRE(scheduled.restart->earliest_restart_tick == before + Seconds(2));
    REQUIRE(match->GetMatchTime_ms() == football_clock);
    REQUIRE(match->GetResetSequence() == resets);
    simulation.Step({});
    REQUIRE(match->GetTimelineTick() == before + TickSpan{2});
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE(match->GetReferee()->GetBuffer().taker != nullptr);
    REQUIRE(match->GetMatchTime_ms() == football_clock);
    REQUIRE(simulation.Observe().restart_pending);
    for (int i = 0; i < 210; ++i) simulation.Step({});
    REQUIRE_FALSE(match->IsInPlay()); // A minimum is not automatic permission.
    REQUIRE(match->GetResetSequence() == resets + 1);
  }
}

TEST_CASE("Restart setup and timeout fire once across timeline jumps",
          "[sim][tick][referee][restart]") {
  for (const bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.half_duration_ms = 18000;
    Simulation simulation;
    Init(simulation, options);
    while (!simulation.IsInPlay()) simulation.Step({});
    for (int i = 0; i < 40; ++i) simulation.Step({});
    Match* match = simulation.match();
    match->GetBall()->ResetSituation(blunted::Vector3(10.f, 40.f, 0.f));
    simulation.Step({});
    Referee* rules = match->GetReferee();
    const auto process_rules = [&] {
      match->Mirror(reverse, !reverse, reverse);
      rules->Process();
      match->Mirror(reverse, !reverse, reverse);
    };
    const auto scheduled = rules->GetBuffer();
    const auto resets = match->GetResetSequence();
    REQUIRE(scheduled.taker == nullptr);
    match->AdvanceTime(Seconds(3));
    process_rules();
    REQUIRE(rules->GetBuffer().taker != nullptr);
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE_FALSE(match->IsInPlay());
    auto rng = match->rng().engine();
    process_rules();
    REQUIRE(match->GetResetSequence() == resets + 1);
    REQUIRE(match->rng().engine() == rng);
    const auto timeout = scheduled.restart->timeout_tick;
    match->AdvanceTime((timeout - match->GetTimelineTick()) + TickSpan{3});
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
    match->GetTeam(0)->SetLastTouchPlayer(player, e_TouchType_Intentional_Kicked);
    REQUIRE(player->GetLastTouchTick() == now);
    REQUIRE(player->GetLastTouchTime_ms() == ToMilliseconds(now));
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

}  // namespace
