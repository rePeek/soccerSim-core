#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/match/match.hpp"
#include "sim/simulation.hpp"
#include "sim/team/possession.hpp"
#include "sim/player/possession.hpp"

namespace {
using football::sim::EvaluatePossession;
using blunted::Vector3;

// Test-only team arrival inputs, not a production state-injection API.
struct ArrivalTeam : Team {
  explicit ArrivalTeam(int side)
      : Team(side, football::app::fixtures::MakeDefaultHomeTeam(), 1.0f) {}
  void Arrival(int milliseconds) { timeNeededToGetToBall_ms = milliseconds; }
};

TEST_CASE("possession selects the fastest team or the current team's candidate on a tie",
          "[sim][possession]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), {});
  Match& match = *simulation.match();
  auto* current = match.GetTeam(0)->GetAllPlayers()[1];
  auto* home_candidate = match.GetTeam(0)->GetAllPlayers()[2];
  auto* away_candidate = match.GetTeam(1)->GetAllPlayers()[2];
  match.GetTeam(0)->SetDesignatedTeamPossessionPlayer(home_candidate);
  ArrivalTeam first(0), second(1);
  first.SetDesignatedTeamPossessionPlayer(home_candidate);
  second.SetDesignatedTeamPossessionPlayer(away_candidate);
  const auto rng = match.rng().engine();
  const auto original_designated = match.GetDesignatedPossessionPlayer();

  first.Arrival(100); second.Arrival(101);
  auto selection = EvaluatePossession(first, second, current, nullptr);
  REQUIRE(selection.best_team == &first);
  REQUIRE(selection.designated_player == current); // Equal player times: hysteresis.
  first.Arrival(101); second.Arrival(100);
  selection = EvaluatePossession(first, second, current, nullptr);
  REQUIRE(selection.best_team == &second);
  REQUIRE(selection.designated_player == current);
  selection = EvaluatePossession(second, first, current, nullptr);
  REQUIRE(selection.best_team == &second); // Processing order does not break ties.
  first.Arrival(100); second.Arrival(100);
  selection = EvaluatePossession(first, second, current, nullptr);
  REQUIRE(selection.best_team == nullptr);
  REQUIRE(selection.designated_player == home_candidate);
  REQUIRE(match.GetDesignatedPossessionPlayer() == original_designated);
  REQUIRE(match.GetBallRetainer() == nullptr);
  REQUIRE(match.rng().engine() == rng);
}

TEST_CASE("physical retention overrides selection without changing the retention fact",
          "[sim][possession]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), {});
  Match& match = *simulation.match();
  auto* retainer = match.GetTeam(1)->GetAllPlayers()[1];
  ArrivalTeam first(0), second(1); // No designated players: retainer must bypass reads.
  first.Arrival(0); second.Arrival(100000);
  const auto selection = EvaluatePossession(first, second, nullptr, retainer);
  REQUIRE(selection.best_team == retainer->GetTeam());
  REQUIRE(selection.designated_player == retainer);
  REQUIRE(match.GetBallRetainer() == nullptr); // Caller alone owns the input fact.
}

TEST_CASE("possession hysteresis preserves unsigned add, float ratio and strict threshold",
          "[sim][possession]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), {});
  Match& match = *simulation.match();
  auto* current = match.GetTeam(0)->GetAllPlayers()[1];
  auto* candidate = match.GetTeam(1)->GetAllPlayers()[1];
  ArrivalTeam first(0), second(1);
  first.Arrival(100); second.Arrival(0);
  second.SetDesignatedTeamPossessionPlayer(candidate);
  match.GetBall()->ResetSituation(Vector3(0));
  bool switched = false, retained = false;
  for (float distance : {0.f, 1.f, 4.f, 8.f, 16.f}) {
    candidate->ResetPosition(Vector3(distance, 0, 0), Vector3(0));
    // Observe a complete refresh cadence; initialization may start mid-animation.
    for (int tick = 0; tick < 10; ++tick) {
      candidate->UpdatePossessionStats(*match.GetBall(), *match.GetTeam(0),
          match.GetTimelineTick(), match.GetBallRetainer());
      simulation.AdvanceTime(football::sim::TickSpan{1});
    }
    const unsigned int old_time = current->GetTimeNeededToGetToBall_ms();
    const unsigned int new_time = candidate->GetTimeNeededToGetToBall_ms();
    const float rating = float(new_time + 10) / float(old_time + 10);
    const auto rng = match.rng().engine();
    const auto selection = EvaluatePossession(first, second, current, nullptr);
    REQUIRE(selection.best_team == &second);
    REQUIRE(selection.designated_player == (rating < 0.85f ? candidate : current));
    REQUIRE(match.rng().engine() == rng);
    switched |= selection.designated_player == candidate;
    retained |= selection.designated_player == current;
  }
  REQUIRE(switched);
  REQUIRE(retained);
  const auto selection = EvaluatePossession(first, second, candidate, nullptr);
  REQUIRE(selection.designated_player == candidate);
}

}  // namespace

template<class T> concept HasTeamTick = requires(T& team) { &T::Process; };
template<class T> concept HasTeamRefresh = requires(T& team) { team.UpdatePossessionStats(); };
template<class T> concept HasTeamRestartQuery = requires(T& team) { team.GetPieceTaker(); } ||
    requires(T& team) { team.GetSetPieceType(); };
static_assert(!HasTeamTick<Team>);
static_assert(!HasTeamRefresh<Team>);
static_assert(!HasTeamRestartQuery<Team>);

TEST_CASE("Roster possession phases consume supplied opponent and physical retainer facts",
          "[sim][possession][dependency]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto& team = *match.GetTeam(0);
  ArrivalTeam opponent(1);
  opponent.Arrival(200);
  const auto rng = match.rng().engine();
  const float amount = float(200 + 1500) / float(team.GetTimeNeededToGetToBall_ms() + 1500);
  const float expected = 1.f + blunted::clamp(
      1.f * 0.995f + blunted::clamp(amount, 0.5f, 1.5f) * 0.005f - 1.f, -0.005f, 0.005f);
  football::sim::player::PrepareTeamPossession(team, opponent, true, false, nullptr, nullptr);
  REQUIRE(team.GetTeamPossessionAmount() == amount);
  REQUIRE(team.GetFadingTeamPossessionAmount() == expected);
  auto* retainer = match.GetTeam(1)->GetAllPlayers()[1];
  football::sim::player::PrepareTeamPossession(team, opponent, true, false, retainer, &team);
  REQUIRE(team.GetTeamPossessionAmount() == 0.5f);
  REQUIRE(team.GetFadingTeamPossessionAmount() == 0.5f);
  Ball supplied(match.pitch());
  football::sim::player::RefreshTeamPossession(team, opponent, supplied, football::sim::Tick{123},
      team.GetAllPlayers()[1]);
  REQUIRE(team.HasPossession());
  REQUIRE(team.GetTimeNeededToGetToBall_ms() == 1);
  REQUIRE(team.GetAllPlayers()[1]->HasUniquePossession());
  REQUIRE(match.GetBallRetainer() == nullptr);
  REQUIRE(match.rng().engine() == rng);
}
