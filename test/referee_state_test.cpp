#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match/match.hpp"
#include "sim/simulation.hpp"

namespace {
using namespace football::sim;
using blunted::Vector3;

struct RefereeStateFixture : Referee {
  using Referee::Referee;
  using Referee::buffer;
  using Referee::foul;
  using Referee::offsidePlayers;
  using Referee::post_restart_relax_;
};

TEST_CASE("period end updates only referee facts from explicit inputs", "[sim][referee][period]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto& kickoff_team = *match.GetTeam(1);
  auto* actor = kickoff_team.GetAllPlayers()[1];
  for (auto phase : {MatchPhase::FirstHalf, MatchPhase::SecondHalf}) {
    RefereeStateFixture referee(&match);
    referee.buffer.taker = actor;
    referee.buffer.restart.emplace();
    referee.buffer.restart->phase = RestartPhase::Pending;
    referee.foul.foulPlayer = actor;
    referee.foul.foulVictim = actor;
    referee.foul.foulType = 3;
    referee.foul.advantage = true;
    referee.foul.foul_tick = Tick{7};
    referee.foul.hasBeenProcessed = false;
    referee.offsidePlayers = {actor};
    referee.post_restart_relax_ = TickSpan{12};
    const auto original = referee.buffer;
    const auto rng = match.rng().engine();
    const auto before = simulation.Observe();
    referee.OnPeriodEnded(phase, Tick{123}, Vector3(3, 4, 0), kickoff_team);
    REQUIRE(referee.buffer.taker == nullptr);
    REQUIRE_FALSE(referee.buffer.restart);
    REQUIRE(referee.post_restart_relax_ == TickSpan{12});
    REQUIRE(referee.offsidePlayers == std::vector<Player*>{actor});
    REQUIRE(referee.foul.foulVictim == actor); // Preserve historically untouched facts.
    if (phase == MatchPhase::FirstHalf) {
      REQUIRE(referee.foul.foulPlayer == nullptr);
      REQUIRE(referee.foul.foulType == 0);
      REQUIRE_FALSE(referee.foul.advantage);
      REQUIRE(referee.foul.foul_tick == Tick{});
      REQUIRE(referee.foul.hasBeenProcessed);
      REQUIRE(referee.buffer.active);
      REQUIRE(referee.buffer.endPhase);
      REQUIRE(referee.buffer.desiredSetPiece == e_GameMode_KickOff);
      REQUIRE(referee.buffer.stop_tick == Tick{123});
      REQUIRE(referee.buffer.prepare_tick == Tick{133});
      REQUIRE(referee.buffer.start_tick == Tick{153});
      REQUIRE(referee.buffer.restartPos == Vector3(3, 4, 0));
      REQUIRE(referee.buffer.teamID == 1);
      REQUIRE(referee.buffer.setpiece_team == &kickoff_team);
    } else {
      REQUIRE_FALSE(referee.buffer.active);
      REQUIRE_FALSE(referee.buffer.endPhase);
      REQUIRE(referee.foul.foulPlayer == actor);
      REQUIRE(referee.foul.foulType == 3);
      REQUIRE(referee.foul.advantage);
      REQUIRE(referee.foul.foul_tick == Tick{7});
      REQUIRE_FALSE(referee.foul.hasBeenProcessed);
      REQUIRE(referee.buffer.stop_tick == original.stop_tick);
      REQUIRE(referee.buffer.prepare_tick == original.prepare_tick);
      REQUIRE(referee.buffer.start_tick == original.start_tick);
      REQUIRE(referee.buffer.teamID == original.teamID);
      REQUIRE(referee.buffer.setpiece_team == original.setpiece_team);
      REQUIRE(referee.buffer.restartPos == original.restartPos);
    }
    REQUIRE(match.rng().engine() == rng);
    const auto after = simulation.Observe();
    REQUIRE(after.tick == before.tick);
    REQUIRE(after.phase == before.phase);
    REQUIRE(after.half_underway == before.half_underway);
    REQUIRE(after.ball_in_play == before.ball_in_play);
    REQUIRE(after.reset_sequence == before.reset_sequence);
    REQUIRE(after.ball_position == before.ball_position);
    REQUIRE(after.regulation_time == before.regulation_time);
    REQUIRE(after.ball_in_play_time == before.ball_in_play_time);
    REQUIRE(match.GetTeam(0)->GetStaticSide() == -1);
    REQUIRE(match.GetTeam(1)->GetStaticSide() == 1);
  }
}

TEST_CASE("Referee invokes the explicit reset action synchronously once before restart planning",
          "[sim][referee][reset][history]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options; options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
    football::test::TakeKickOff(simulation);
    for (int tick = 0; tick < 40; ++tick) simulation.Step({});
    auto& match = *simulation.match();
    auto& referee = *match.GetReferee();
    match.GetBall()->ResetSituation(Vector3(10, 40, 0));
    simulation.Step({}); // Out-of-play classification, no setup/reset yet.
    REQUIRE(referee.GetBuffer().restart);
    REQUIRE_FALSE(referee.GetBuffer().restart->setup_done);
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
    const auto sequence = match.GetResetSequence();
    const auto now = match.GetTimelineTick();
    int calls = 0;
    simulation.Mirror(reverse, !reverse, false);
    referee.Process([&](const Vector3& focus) {
      REQUIRE(++calls == 1);
      REQUIRE_FALSE(referee.GetBuffer().restart->setup_done);
      REQUIRE(referee.GetBuffer().taker == nullptr);
      REQUIRE(match.GetResetSequence() == sequence);
      REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
      simulation.ResetSituation(focus);
      REQUIRE(match.GetResetSequence() == sequence + 1);
      REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
      REQUIRE(match.GetTimelineTick() == now);
    });
    REQUIRE(calls == 1);
    REQUIRE(referee.GetBuffer().restart->setup_done);
    REQUIRE(referee.GetBuffer().taker != nullptr);
    REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
    const auto rng = match.rng().engine();
    // This fresh per-call action must not be called again on the same pending restart.
    referee.Process([](const Vector3&) { FAIL("restart reset was repeated"); });
    REQUIRE(match.GetResetSequence() == sequence + 1);
    REQUIRE(match.rng().engine() == rng);
    simulation.Mirror(reverse, !reverse, false);
    simulation.Step({});
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
  }
}

}  // namespace
