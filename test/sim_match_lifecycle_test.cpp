#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "app/fixtures/default_teams.hpp"
#include "sim/match.hpp"
#include "sim/simulation.hpp"

namespace {
void Init(Simulation& simulation, MatchOptions options = {}) {
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), options, false);
}
std::uint64_t Finish(Simulation& simulation) {
  std::uint64_t steps = 0;
  while (!simulation.Finished()) {
    simulation.Step({}); REQUIRE(++steps < 2000);
  }
  return steps;
}
}

TEST_CASE("sim projects referee phases and a paused scaled football clock", "[sim][lifecycle]") {
  for (bool reverse : {false, true}) {
    MatchOptions options; options.half_duration_ms = 1800; options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    const auto initial = simulation.Observe();
    REQUIRE(initial.phase == MatchPhase::PreMatch);
    REQUIRE(initial.match_time_ms == 0);
    REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    simulation.Step({});
    REQUIRE(simulation.Observe().phase == MatchPhase::FirstHalf);
    REQUIRE(simulation.Observe().match_time_ms == 0);
    std::uint64_t steps = 1;
    bool second = false, second_play = false;
    while (!simulation.Finished()) {
      const auto before = simulation.Observe();
      simulation.Step({}); ++steps; REQUIRE(steps < 1000);
      const auto after = simulation.Observe();
      REQUIRE(after.match_time_ms == simulation.match()->GetMatchTime_ms());
      REQUIRE(after.phase == simulation.match()->GetMatchPhase());
      REQUIRE(after.match_time_ms >= before.match_time_ms);
      if (!before.in_play && !after.in_play) REQUIRE(before.match_time_ms == after.match_time_ms);
      if (after.phase == MatchPhase::SecondHalf) {
        second = true; second_play |= after.in_play;
        REQUIRE(after.match_time_ms >= options.half_duration_ms);
      }
    }
    REQUIRE(second); REQUIRE(second_play);
    const auto world = simulation.Observe();
    const auto result = simulation.Result();
    REQUIRE(world.phase == MatchPhase::Finished);
    REQUIRE(world.match_time_ms == 2 * options.half_duration_ms);
    REQUIRE_FALSE(world.in_play); REQUIRE_FALSE(world.in_set_piece);
    REQUIRE(world.restart == e_GameMode_Normal); REQUIRE_FALSE(world.restart_taker);
    REQUIRE(result.duration_ticks == steps);
    REQUIRE(result.outcome == MatchOutcome::Draw);
    const auto rng = simulation.match()->rng().engine();
    for (int tick = 0; tick < 20; ++tick) simulation.Step({});
    REQUIRE(simulation.Result() == result);
    REQUIRE(simulation.match()->rng().engine() == rng);
    REQUIRE(simulation.Observe().tick == world.tick);
    REQUIRE(simulation.Observe().ball_position == world.ball_position);
    REQUIRE(simulation.Observe().match_time_ms == world.match_time_ms);
    simulation.Stop(); REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    REQUIRE(initial.phase == MatchPhase::PreMatch); REQUIRE(initial.match_time_ms == 0);
    Init(simulation, options);
    REQUIRE(simulation.Observe().simulation_epoch != initial.simulation_epoch);
    REQUIRE(Finish(simulation) == steps);
    REQUIRE(simulation.Result() == result);
  }
}

TEST_CASE("a sub-tick regulation period still plays both halves", "[sim][lifecycle]") {
  MatchOptions options; options.half_duration_ms = 1;
  Simulation simulation; Init(simulation, options);
  bool first = false, second = false;
  for (int tick = 0; tick < 1000 && !simulation.Finished(); ++tick) {
    simulation.Step({}); const auto world = simulation.Observe();
    first |= world.phase == MatchPhase::FirstHalf && world.in_play;
    second |= world.phase == MatchPhase::SecondHalf && world.in_play;
  }
  REQUIRE(first); REQUIRE(second); REQUIRE(simulation.Finished());
  REQUIRE(simulation.Observe().match_time_ms == 2);
}

TEST_CASE("full time takes priority over pending restarts", "[sim][lifecycle]") {
  MatchOptions options; options.half_duration_ms = 1800;
  Simulation simulation; Init(simulation, options);
  while (simulation.Observe().phase != MatchPhase::SecondHalf || !simulation.IsInPlay())
    simulation.Step({});
  Match* match = simulation.match();
  match->StartSetPiece();
  match->BumpActualTime_ms(1000);  // Advance football time through sim authority, not a fake snapshot.
  const auto resets = match->GetResetSequence();
  simulation.Step({});
  REQUIRE(simulation.Finished());
  REQUIRE_FALSE(match->IsInSetPiece());
  REQUIRE_FALSE(match->GetReferee()->GetBuffer().active);
  REQUIRE(match->GetResetSequence() == resets);
  REQUIRE(simulation.Result().outcome == MatchOutcome::Draw);
}

TEST_CASE("final outcomes derive from actual simulation goals and stable home away scores", "[sim][lifecycle]") {
  for (bool home_win : {true, false}) {
    MatchOptions options; options.half_duration_ms = 1800;
    Simulation simulation; Init(simulation, options);
    while (!simulation.IsInPlay()) simulation.Step({});
    auto* match = simulation.match();
    const float direction = home_win ? 1.f : -1.f;
    match->GetBall()->ResetSituation(blunted::Vector3(direction * 54.9f, 0.f, 0.5f));
    match->GetBall()->Touch(blunted::Vector3(direction * 30.f, 0.f, 0.f));
    for (int step = 0; step < 5 && simulation.Observe().teams[home_win ? 0 : 1].score == 0; ++step)
      simulation.Step({});
    CAPTURE(home_win, simulation.Observe().ball_position.coords[0], match->IsInPlay(),
            simulation.Observe().teams[0].score, simulation.Observe().teams[1].score);
    REQUIRE(simulation.Observe().teams[home_win ? 0 : 1].score == 1);
    Finish(simulation);
    const auto result = simulation.Result();
    REQUIRE(result.home_score == (home_win ? 1 : 0));
    REQUIRE(result.away_score == (home_win ? 0 : 1));
    REQUIRE(result.outcome == (home_win ? MatchOutcome::HomeWin : MatchOutcome::AwayWin));
  }
}

TEST_CASE("invalid time rules are rejected before publishing a match", "[sim][lifecycle]") {
  Simulation simulation;
  for (auto duration : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(), 100.f}) {
    MatchOptions options; options.match_duration = duration;
    if (duration == 0.f) options.half_duration_ms = 0;
    REQUIRE_THROWS_AS(Init(simulation, options), std::invalid_argument);
    REQUIRE(simulation.match() == nullptr);
    REQUIRE_FALSE(simulation.Finished());
  }
  MatchOptions overflow; overflow.half_duration_ms = std::numeric_limits<std::uint64_t>::max();
  REQUIRE_THROWS_AS(Init(simulation, overflow), std::invalid_argument);
  REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
  REQUIRE_THROWS_AS(simulation.Observe(), std::logic_error);
  REQUIRE_THROWS_AS(simulation.Step({}), std::logic_error);
  MatchOptions good; good.half_duration_ms = 1800;
  Init(simulation, good); REQUIRE(Finish(simulation) > 0);
}

TEST_CASE("football clock keeps integer progress beyond float precision", "[sim][lifecycle]") {
  MatchOptions options; options.half_duration_ms = UINT64_C(1) << 40;
  options.match_duration = 49.75f; // Legacy factor 10: one football ms per step.
  Simulation simulation; Init(simulation, options);
  while (!simulation.IsInPlay()) simulation.Step({});
  auto* match = simulation.match();
  match->BumpActualTime_ms(200000000UL);
  const auto clock = simulation.Observe().match_time_ms;
  REQUIRE(clock > (UINT64_C(1) << 24));
  match->BumpActualTime_ms(10);
  REQUIRE(simulation.Observe().match_time_ms == clock + 1);
}

TEST_CASE("result counts executed steps rather than compressed restart time", "[sim][lifecycle]") {
  MatchOptions options; options.half_duration_ms = 18000;
  Simulation simulation; Init(simulation, options);
  std::uint64_t steps = 0;
  while (!simulation.IsInPlay()) { simulation.Step({}); ++steps; }
  // Push the ball over the touchline; the referee fast-forwards the clock while
  // the restart is prepared, so elapsed tick time outruns executed Step calls.
  const auto resets = simulation.Observe().reset_sequence;
  simulation.match()->GetBall()->ResetSituation(blunted::Vector3(10.f, 40.f, 0.f));
  for (int attempt = 0; attempt < 200 && simulation.Observe().reset_sequence == resets; ++attempt) {
    simulation.Step({}); REQUIRE(++steps < 3000);
  }
  REQUIRE(simulation.Observe().reset_sequence > resets);
  REQUIRE(simulation.Observe().tick > steps);
  while (!simulation.Finished()) { simulation.Step({}); REQUIRE(++steps < 2000); }
  REQUIRE(simulation.Result().duration_ticks == steps);
  REQUIRE(simulation.Observe().tick > steps);
}
