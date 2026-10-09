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

}  // namespace
