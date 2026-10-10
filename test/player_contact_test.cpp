#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "football/ball/ball.hpp"
#include "sim/player/player_contact.hpp"
#include "sim/simulation.hpp"

using football::ball::Ball;

namespace {
using namespace football::sim;
using football::sim::testing::SimulationAccess;
using blunted::Vector3;

// Records the immutable contact facts the solver produces; rule verdicts are
// no longer the contact solver's concern.
struct FactProbe final : SimulationFactSink {
  std::vector<football::sim::event::SimulationFact> facts;
  void OnSimulationFact(football::sim::event::SimulationFact fact) override {
    facts.push_back(std::move(fact));
  }
};

struct PlayerContactFixture {
  Simulation simulation;
  Ball ball{football::model::Pitch{}};
  std::array<Player*, 3> players;

  explicit PlayerContactFixture(bool reverse) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(),
        football::model::Pitch{}, options);
    players = {SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation))->GetAllPlayers()[1],
               SimulationAccess::TeamOf(simulation, SimulationAccess::SecondTeamOf(simulation))->GetAllPlayers()[1],
               SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation))->GetAllPlayers()[2]};
    ball.ResetSituation(Vector3(20, 20, 0));
    Reset();
  }

  void Reset() {
    for (std::size_t i = 0; i < players.size(); ++i) {
      const Vector3 position(static_cast<float>(i) * 0.5f, 0, 0);
      players[i]->ResetPosition(position, position + Vector3(1, 0, 0));
      REQUIRE(players[i]->GetMovement() == Vector3(0));
    }
  }

  FactProbe probe;

  std::array<Vector3, 3> Positions() const {
    return {players[0]->GetPosition(), players[1]->GetPosition(), players[2]->GetPosition()};
  }
};

TEST_CASE("player contacts accept empty and single-player spans without side effects",
          "[sim][player][contact]") {
  PlayerContactFixture fixture(false);
  const auto before = fixture.Positions();
  const auto rng = SimulationAccess::RngOf(fixture.simulation).engine();
  ResolvePlayerContacts({Tick{19}, {}, fixture.ball, nullptr}, fixture.probe);
  ResolvePlayerContacts({Tick{19}, std::span<Player* const>(fixture.players.data(), 1),
                         fixture.ball, fixture.players[0]}, fixture.probe);
  REQUIRE(fixture.Positions() == before);
  REQUIRE(SimulationAccess::RngOf(fixture.simulation).engine() == rng);
}

TEST_CASE("player contacts mutate pairs in caller order before the next pair",
          "[sim][player][contact]") {
  for (bool reverse : {false, true}) {
    PlayerContactFixture fixture(reverse);
    const auto before = fixture.Positions();
    const auto rng = SimulationAccess::RngOf(fixture.simulation).engine();
    std::vector<Vector3> predictions;
    fixture.ball.GetPredictionArray(predictions);
    ResolvePlayerContacts({Tick{19}, fixture.players, fixture.ball, nullptr}, fixture.probe);
    const auto sweep = fixture.Positions();
    REQUIRE(sweep != before);
    REQUIRE(SimulationAccess::RngOf(fixture.simulation).engine() == rng);
    std::vector<Vector3> after;
    fixture.ball.GetPredictionArray(after);
    REQUIRE(after == predictions);

    fixture.Reset();
    // These stationary actors have no movement-sharing offset. Each two-player
    // invocation is therefore an independent oracle for the in-place pair sweep.
    for (std::size_t i = 0; i < 2; ++i) {
      for (std::size_t j = i + 1; j < 3; ++j) {
        const std::array<Player*, 2> pair{fixture.players[i], fixture.players[j]};
        ResolvePlayerContacts({Tick{19}, pair, fixture.ball, nullptr}, fixture.probe);
      }
    }
    REQUIRE(fixture.Positions() == sweep);

    fixture.Reset();
    const std::array<Player*, 3> reversed{
        fixture.players[2], fixture.players[1], fixture.players[0]};
    ResolvePlayerContacts({Tick{19}, reversed, fixture.ball, nullptr}, fixture.probe);
    REQUIRE(fixture.Positions() != sweep); // No sorting or frozen-position batch.
  }
}

TEST_CASE("player contacts use the explicit possession designation and Ball",
          "[sim][player][contact]") {
  PlayerContactFixture fixture(false);
  const std::array<Player*, 2> pair{fixture.players[0], fixture.players[1]};
  ResolvePlayerContacts({Tick{19}, pair, fixture.ball, pair[0]}, fixture.probe);
  const auto first_designated = fixture.Positions();
  fixture.Reset();
  ResolvePlayerContacts({Tick{19}, pair, fixture.ball, pair[1]}, fixture.probe);
  REQUIRE(fixture.Positions() != first_designated);
  REQUIRE(SimulationAccess::DesignatedPlayerOf(fixture.simulation) != pair[0]);
  REQUIRE(SimulationAccess::DesignatedPlayerOf(fixture.simulation) != pair[1]);
}

TEST_CASE("foul assessment returns at most one offender per collision",
          "[sim][player][contact][foul]") {
  PlayerContactFixture fixture(false);
  Player* home = fixture.players[0];
  Player* away = fixture.players[1];
  Player* home_teammate = fixture.players[2];
  REQUIRE(home->GetTeamID() != away->GetTeamID());
  REQUIRE(home->GetTeamID() == home_teammate->GetTeamID());

  // Harmless contact: no suspect.
  REQUIRE_FALSE(AssessCollision(home, away, 0.2f, 0.4f).has_value());

  // Only one actor falls hard: the other is the suspected offender.
  auto first_falls = AssessCollision(home, away, 0.8f, 0.1f);
  REQUIRE(first_falls.has_value());
  REQUIRE(first_falls->victim == home->GetID());
  REQUIRE(first_falls->offender == away->GetID());
  REQUIRE(first_falls->score > 0.0f);
  REQUIRE(first_falls->score <= 1.0f);

  auto second_falls = AssessCollision(home, away, 0.1f, 0.8f);
  REQUIRE(second_falls.has_value());
  REQUIRE(second_falls->victim == away->GetID());
  REQUIRE(second_falls->offender == home->GetID());

  // Both fall: the harder fall is the victim, so there is still one offender.
  auto both = AssessCollision(home, away, 0.9f, 0.7f);
  REQUIRE(both.has_value());
  REQUIRE(both->victim == home->GetID());
  auto both_reversed = AssessCollision(home, away, 0.7f, 0.9f);
  REQUIRE(both_reversed.has_value());
  REQUIRE(both_reversed->victim == away->GetID());

  // A maximal fall normalizes to the top of the score range.
  auto extreme = AssessCollision(home, away, 1.0f, 0.0f);
  REQUIRE(extreme.has_value());
  REQUIRE(extreme->score == 1.0f);

  // Same-team contacts and null actors are never opponent fouls.
  REQUIRE_FALSE(AssessCollision(home, home_teammate, 0.9f, 0.9f).has_value());
  REQUIRE_FALSE(AssessCollision(home, away, 0.0f, 0.0f).has_value());
  REQUIRE_FALSE(AssessCollision(nullptr, away, 0.9f, 0.0f).has_value());
}

TEST_CASE("foul assessment ties break on PlayerId for determinism",
          "[sim][player][contact][foul]") {
  PlayerContactFixture fixture(false);
  Player* first = fixture.players[0];
  Player* second = fixture.players[1];
  auto lower_first = AssessCollision(first, second, 0.7f, 0.7f);
  auto lower_second = AssessCollision(second, first, 0.7f, 0.7f);
  REQUIRE(lower_first.has_value());
  REQUIRE(lower_second.has_value());
  // Argument order must not change the verdict.
  REQUIRE(lower_first->victim == lower_second->victim);
  const Player* expected = first->GetID() < second->GetID() ? first : second;
  REQUIRE(lower_first->victim == expected->GetID());
}

TEST_CASE("simulation publishes a fresh foul assessment set each tick",
          "[sim][player][contact][foul]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  REQUIRE(SimulationAccess::FoulAssessmentsOf(simulation).empty());
  const PlayerControlSet controls;
  simulation.Step(controls);
  const auto& published = SimulationAccess::FoulAssessmentsOf(simulation);
  // At most one suspected offender per unordered pair.
  REQUIRE(published.size() <= 231u);  // C(22, 2)
  for (const auto& assessment : published) {
    REQUIRE(assessment.score > 0.0f);
    REQUIRE(assessment.score <= 1.0f);
    REQUIRE(assessment.offender != assessment.victim);
  }
}
}  // namespace
