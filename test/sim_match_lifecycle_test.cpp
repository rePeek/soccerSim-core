#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match/match.hpp"
#include "sim/simulation.hpp"
#include "sim/team/team.hpp"

namespace {
using namespace football::sim;
void Init(Simulation& simulation, MatchOptions options = {}) {
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), options);
}
void StartHalf(Simulation& simulation) {
  const auto policy = football::test::MakeDefaultAI(simulation);
  for (int attempts = 0; !simulation.Observe().ball_in_play; ++attempts) {
    REQUIRE(attempts < 1000);
    REQUIRE_FALSE(simulation.Finished());
    football::test::StepDefaultAI(simulation, policy);
  }
}
std::uint64_t Finish(Simulation& simulation) {
  const auto policy = football::test::MakeDefaultAI(simulation);
  std::uint64_t steps = 0;
  while (!simulation.Finished()) {
    football::test::StepDefaultAI(simulation, policy);
    REQUIRE(++steps < 10000);
  }
  return steps;
}
}

TEST_CASE("three clocks distinguish half running, authorization and actual ball play", "[sim][lifecycle][clock]") {
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.half_duration = TickSpan{180};
    options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    const auto initial = simulation.Observe();
    REQUIRE(initial.phase == MatchPhase::PreMatch);
    REQUIRE(initial.regulation_time == TickSpan{});
    REQUIRE(initial.ball_in_play_time == TickSpan{});
    REQUIRE_FALSE(initial.half_underway);
    REQUIRE_FALSE(initial.ball_in_play);
    REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    const auto policy = football::test::MakeDefaultAI(simulation);
    std::uint64_t steps = 0;
    bool second = false, second_play = false, break_seen = false;
    while (!simulation.Finished()) {
      const auto before = simulation.Observe();
      football::test::StepDefaultAI(simulation, policy);
      REQUIRE(++steps < 3000);
      const auto after = simulation.Observe();
      REQUIRE(after.regulation_time == simulation.match()->GetRegulationTime());
      REQUIRE(after.ball_in_play_time == simulation.match()->GetBallInPlayTime());
      REQUIRE(after.phase == simulation.match()->GetMatchPhase());
      REQUIRE(after.regulation_time >= before.regulation_time);
      REQUIRE(after.ball_in_play_time >= before.ball_in_play_time);
      REQUIRE(after.regulation_time - before.regulation_time <= TickSpan{1});
      REQUIRE(after.ball_in_play_time - before.ball_in_play_time <=
              after.regulation_time - before.regulation_time);
      REQUIRE(after.ball_in_play_time <= after.regulation_time);
      REQUIRE(after.regulation_time.value <= after.tick);
      if (!before.half_underway && !after.half_underway)
        REQUIRE(before.regulation_time == after.regulation_time);
      if (after.phase == MatchPhase::SecondHalf) {
        second = true; second_play |= after.ball_in_play;
        break_seen |= !after.half_underway;
        REQUIRE(after.regulation_time >= options.half_duration);
      }
    }
    REQUIRE(second); REQUIRE(second_play); REQUIRE(break_seen);
    const auto world = simulation.Observe();
    const auto result = simulation.Result();
    REQUIRE(world.phase == MatchPhase::Finished);
    REQUIRE(world.regulation_time == options.half_duration + options.half_duration);
    REQUIRE_FALSE(world.half_underway); REQUIRE_FALSE(world.ball_in_play);
    REQUIRE_FALSE(world.in_play); REQUIRE_FALSE(world.in_set_piece);
    REQUIRE(world.restart == e_GameMode_Normal); REQUIRE_FALSE(world.restart_taker);
    REQUIRE(result.duration_ticks == steps);
    REQUIRE(world.tick + 1 == steps); // Terminal whistle does not execute physics/time.
    REQUIRE(result.outcome == MatchOutcome::Draw);
    const auto rng = simulation.match()->rng().engine();
    for (int tick = 0; tick < 20; ++tick) simulation.Step({});
    simulation.match()->AdvanceTime(Seconds(1)); // Direct authority is frozen too.
    REQUIRE(simulation.Result() == result);
    REQUIRE(simulation.match()->rng().engine() == rng);
    REQUIRE(simulation.Observe().tick == world.tick);
    REQUIRE(simulation.Observe().ball_position == world.ball_position);
    REQUIRE(simulation.Observe().regulation_time == world.regulation_time);
    REQUIRE(simulation.Observe().ball_in_play_time == world.ball_in_play_time);
    simulation.Stop(); REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    REQUIRE(initial.phase == MatchPhase::PreMatch);
    REQUIRE(initial.regulation_time == TickSpan{});
    Init(simulation, options);
    REQUIRE(Finish(simulation) == steps);
    REQUIRE(simulation.Result() == result);
  }
}

TEST_CASE("idle kickoff authorization cannot start either football clock", "[sim][lifecycle][clock]") {
  for (bool reverse : {false, true}) {
    MatchOptions options; options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    for (int tick = 0; tick < 500; ++tick) simulation.Step({});
    const auto ready = simulation.Observe();
    REQUIRE(ready.tick == 500);
    REQUIRE(ready.in_play); REQUIRE(ready.in_set_piece);
    REQUIRE_FALSE(ready.half_underway); REQUIRE_FALSE(ready.ball_in_play);
    REQUIRE(ready.regulation_time == TickSpan{});
    REQUIRE(ready.ball_in_play_time == TickSpan{});
    REQUIRE(simulation.match()->GetReferee()->GetBuffer().restart->phase == RestartPhase::Ready);
    const auto* taker = simulation.match()->GetReferee()->GetBuffer().taker;
    for (int side : {0, 1})
      for (const auto* actor : simulation.match()->GetTeam(side)->GetAllPlayers())
        REQUIRE(simulation.match()->MayTouchBall(*actor) == (actor == taker));
    StartHalf(simulation);
    const auto taken = simulation.Observe();
    REQUIRE(taken.half_underway); REQUIRE(taken.ball_in_play);
    REQUIRE(taken.regulation_time == TickSpan{1});
    REQUIRE(taken.ball_in_play_time == TickSpan{1});
    const auto& buffer = simulation.match()->GetReferee()->GetBuffer();
    REQUIRE(buffer.restart->phase == RestartPhase::Taken);
    REQUIRE(buffer.taker->GetLastTouchTick().value + 1 == taken.tick);
    REQUIRE(simulation.match()->GetBall()->GetMovement().GetLength() > 0.1f);
    for (int side : {0, 1})
      for (const auto* actor : simulation.match()->GetTeam(side)->GetAllPlayers())
        REQUIRE(simulation.match()->MayTouchBall(*actor));
  }
}

TEST_CASE("one-tick regulation periods each require their own actual kickoff", "[sim][lifecycle][clock]") {
  MatchOptions options; options.half_duration = TickSpan{1};
  Simulation simulation; Init(simulation, options);
  bool first = false, second = false;
  const auto policy = football::test::MakeDefaultAI(simulation);
  for (int tick = 0; tick < 3000 && !simulation.Finished(); ++tick) {
    football::test::StepDefaultAI(simulation, policy);
    const auto world = simulation.Observe();
    first |= world.phase == MatchPhase::FirstHalf && world.ball_in_play;
    second |= world.phase == MatchPhase::SecondHalf && world.ball_in_play;
  }
  REQUIRE(first); REQUIRE(second); REQUIRE(simulation.Finished());
  REQUIRE(simulation.Observe().regulation_time == TickSpan{2});
  REQUIRE(simulation.Observe().ball_in_play_time == TickSpan{2});
}

TEST_CASE("period whistles win over pending dead balls and half-time stops regulation", "[sim][lifecycle][clock][restart]") {
  for (bool reverse : {false, true}) {
    MatchOptions options; options.half_duration = Seconds(10);
    options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    for (int half = 1; half <= 2; ++half) {
      StartHalf(simulation);
      // Release Taken and expire relaxation before forcing a native ordinary out.
      for (int tick = 0; tick < 41; ++tick) simulation.Step({});
      auto* match = simulation.match();
      match->GetBall()->ResetSituation(blunted::Vector3(10.f, 40.f, 0.f));
      simulation.Step({}); simulation.Step({});
      REQUIRE(simulation.Observe().restart_pending);
      REQUIRE(simulation.Observe().half_underway);
      REQUIRE_FALSE(simulation.Observe().ball_in_play);
      const auto resets = match->GetResetSequence();
      const auto rng = match->rng().engine();
      const auto in_play = match->GetBallInPlayTime();
      const auto position = simulation.Observe().ball_position;
      match->AdvanceTime(Seconds(100));
      REQUIRE(match->GetRegulationTime() == (half == 1 ? Seconds(10) : Seconds(20)));
      REQUIRE(match->GetBallInPlayTime() == in_play);
      REQUIRE(match->rng().engine() == rng);
      simulation.Step({});
      REQUIRE_FALSE(simulation.Observe().half_underway);
      REQUIRE_FALSE(simulation.Observe().ball_in_play);
      REQUIRE_FALSE(match->GetReferee()->RestartNeedsSimulation());
      REQUIRE(match->GetResetSequence() == resets);
      REQUIRE(simulation.Observe().ball_position == position);
      if (half == 1) {
        REQUIRE(simulation.Observe().phase == MatchPhase::SecondHalf);
        const auto before = simulation.Observe();
        for (int tick = 0; tick < 60; ++tick) {
          simulation.Step({});
          REQUIRE(simulation.Observe().regulation_time == before.regulation_time);
          REQUIRE(simulation.Observe().ball_in_play_time == before.ball_in_play_time);
        }
        REQUIRE(simulation.Observe().in_set_piece); // Authorized, not actually taken.
      }
    }
    REQUIRE(simulation.Finished());
    REQUIRE_FALSE(simulation.match()->GetReferee()->GetBuffer().active);
    REQUIRE(simulation.Result().outcome == MatchOutcome::Draw);
  }
}

TEST_CASE("final outcomes derive from real goals and stable home away scores", "[sim][lifecycle]") {
  for (bool home_win : {true, false}) {
    MatchOptions options; options.half_duration = TickSpan{180};
    Simulation simulation; Init(simulation, options); StartHalf(simulation);
    auto* match = simulation.match();
    const float direction = home_win ? 1.f : -1.f;
    match->GetBall()->ResetSituation(blunted::Vector3(direction * 54.9f, 0.f, 0.5f));
    match->TouchBall(blunted::Vector3(direction * 30.f, 0.f, 0.f));
    for (int step = 0; step < 5 && simulation.Observe().teams[home_win ? 0 : 1].score == 0; ++step)
      simulation.Step({});
    REQUIRE(simulation.Observe().teams[home_win ? 0 : 1].score == 1);
    REQUIRE_FALSE(simulation.Observe().ball_in_play);
    Finish(simulation);
    const auto result = simulation.Result();
    REQUIRE(result.home_score == (home_win ? 1 : 0));
    REQUIRE(result.away_score == (home_win ? 0 : 1));
    REQUIRE(result.outcome == (home_win ? MatchOutcome::HomeWin : MatchOutcome::AwayWin));
  }
}

TEST_CASE("invalid native durations fail before match publication or RNG consumption", "[sim][lifecycle][clock]") {
  Simulation simulation;
  for (auto duration : {TickSpan{}, TickSpan{std::numeric_limits<std::uint64_t>::max()},
                         TickSpan{std::numeric_limits<std::uint64_t>::max() / 2 + 1}}) {
    MatchOptions options; options.half_duration = duration;
    REQUIRE_THROWS_AS(Init(simulation, options), std::invalid_argument);
    REQUIRE(simulation.match() == nullptr); REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    REQUIRE_THROWS_AS(simulation.Observe(), std::logic_error);
    REQUIRE_THROWS_AS(simulation.Step({}), std::logic_error);
  }
  Init(simulation);
  Simulation fresh; Init(fresh);
  REQUIRE(simulation.match()->rng().engine() == fresh.match()->rng().engine());
  simulation.Stop();
  MatchOptions large; large.half_duration = TickSpan{std::numeric_limits<std::uint64_t>::max() / 2};
  REQUIRE_NOTHROW(Init(simulation, large)); // No spurious millisecond-capacity restriction.
}

TEST_CASE("native clocks retain integer precision and advance atomically", "[sim][lifecycle][clock]") {
  MatchOptions options; options.half_duration = TickSpan{UINT64_C(1) << 40};
  Simulation simulation; Init(simulation, options); StartHalf(simulation);
  auto* match = simulation.match();
  match->AdvanceTime(TickSpan{UINT64_C(1) << 25});
  const auto before = simulation.Observe();
  const auto rng = match->rng().engine();
  match->AdvanceTime(TickSpan{1});
  REQUIRE(simulation.Observe().regulation_time == before.regulation_time + TickSpan{1});
  REQUIRE(simulation.Observe().ball_in_play_time == before.ball_in_play_time + TickSpan{1});
  REQUIRE(simulation.Observe().tick == before.tick + 1);
  REQUIRE(simulation.Observe().ball_position == before.ball_position);
  REQUIRE(match->rng().engine() == rng);
  const auto stable = simulation.Observe();
  REQUIRE_THROWS_AS(match->AdvanceTime(TickSpan{std::numeric_limits<std::uint64_t>::max()}),
                    std::overflow_error);
  REQUIRE(simulation.Observe().tick == stable.tick);
  REQUIRE(simulation.Observe().regulation_time == stable.regulation_time);
  REQUIRE(simulation.Observe().ball_in_play_time == stable.ball_in_play_time);
  REQUIRE(match->rng().engine() == rng);
}

TEST_CASE("half time changes ends but keeps observations in the canonical home frame", "[sim][lifecycle]") {
  const auto observed_x = [](const Simulation& simulation, int team) {
    return simulation.Observe().players[team == 0 ? 0 : 11].position.coords[0];
  };
  const auto keeper_pitch_x = [](Simulation& simulation, int team) {
    return simulation.match()->GetTeam(team)->GetAllPlayers()[0]->GetPitchPosition().coords[0];
  };
  MatchOptions options; options.half_duration = TickSpan{180};
  Simulation simulation; Init(simulation, options); StartHalf(simulation);
  const auto policy = football::test::MakeDefaultAI(simulation);
  REQUIRE(simulation.Observe().phase == MatchPhase::FirstHalf);
  REQUIRE(simulation.match()->GetTeam(0)->GetStaticSide() == -1);
  REQUIRE(simulation.match()->GetTeam(1)->GetStaticSide() == 1);
  REQUIRE(keeper_pitch_x(simulation, 0) < 0.f); REQUIRE(keeper_pitch_x(simulation, 1) > 0.f);
  REQUIRE(observed_x(simulation, 0) < 0.f); REQUIRE(observed_x(simulation, 1) > 0.f);
  for (int attempts = 0; !(simulation.Observe().phase == MatchPhase::SecondHalf &&
                            simulation.Observe().ball_in_play); ++attempts) {
    REQUIRE(attempts < 1000); REQUIRE_FALSE(simulation.Finished());
    football::test::StepDefaultAI(simulation, policy);
  }
  REQUIRE(simulation.match()->GetTeam(0)->GetStaticSide() == 1);
  REQUIRE(simulation.match()->GetTeam(1)->GetStaticSide() == -1);
  REQUIRE(keeper_pitch_x(simulation, 0) > 0.f); REQUIRE(keeper_pitch_x(simulation, 1) < 0.f);
  REQUIRE(observed_x(simulation, 0) < 0.f); REQUIRE(observed_x(simulation, 1) > 0.f);
  REQUIRE(simulation.Observe().teams[0].defending_direction == -1);
  REQUIRE(simulation.Observe().teams[1].defending_direction == 1);
  REQUIRE_FALSE(simulation.match()->isBallMirrored());
  REQUIRE_FALSE(simulation.match()->GetTeam(0)->isMirrored());
  REQUIRE_FALSE(simulation.match()->GetTeam(1)->isMirrored());
}

TEST_CASE("the same physical goal credits opposite teams in the two halves", "[sim][lifecycle]") {
  const auto credited = [](MatchPhase wanted, float direction) {
    MatchOptions options; options.half_duration = TickSpan{180};
    Simulation simulation; Init(simulation, options);
    const auto policy = football::test::MakeDefaultAI(simulation);
    for (int attempt = 0; attempt < 3000 && !simulation.Finished(); ++attempt) {
      if (simulation.Observe().phase == wanted && simulation.Observe().ball_in_play) break;
      football::test::StepDefaultAI(simulation, policy);
    }
    REQUIRE(simulation.Observe().phase == wanted);
    REQUIRE(simulation.Observe().ball_in_play);
    Match* match = simulation.match();
    const int before[2] = {match->GetScore(0), match->GetScore(1)};
    match->GetBall()->ResetSituation(blunted::Vector3(direction * 54.9f, 0.f, 0.5f));
    match->TouchBall(blunted::Vector3(direction * 30.f, 0.f, 0.f));
    for (int step = 0; step < 12; ++step) {
      simulation.Step({});
      if (match->GetScore(0) != before[0]) return 0;
      if (match->GetScore(1) != before[1]) return 1;
    }
    return -1;
  };
  REQUIRE(credited(MatchPhase::FirstHalf, 1.f) == 0);
  REQUIRE(credited(MatchPhase::FirstHalf, -1.f) == 1);
  REQUIRE(credited(MatchPhase::SecondHalf, 1.f) == 1);
  REQUIRE(credited(MatchPhase::SecondHalf, -1.f) == 0);
}
