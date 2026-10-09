#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "rule_command_fixture.hpp"
#include "sim/simulation.hpp"
#include "sim/testing/simulation_access.hpp"

namespace {

using blunted::Vector3;
using football::sim::testing::SimulationAccess;

TEST_CASE("an injected pitch drives referee, observation and restart geometry",
          "[sim][custom-pitch]") {
  const football::model::Pitch custom(100.0f, 64.0f, 0.04f, 1.6f, 0.025f);
  REQUIRE(custom.half_length() == 50.0f);
  REQUIRE(custom.half_width() == 32.0f);

  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                  football::app::fixtures::MakeDefaultAwayTeam(), custom, {});

  // Every runtime reader shares the same injected description.
  REQUIRE(SimulationAccess::PitchOf(simulation).half_length() == 50.0f);
  REQUIRE(SimulationAccess::PitchOf(simulation).half_width() == 32.0f);
  REQUIRE(simulation.Observe().pitch.half_length() == 50.0f);
  REQUIRE(simulation.Observe().pitch.half_width() == 32.0f);

  // Referee uses the injected boundary, not the legacy 55/36.
  auto facts = SimulationAccess::RefereeFactsOf(simulation);
  facts.phase = MatchPhase::FirstHalf;
  facts.now = football::sim::Tick{9001};
  facts.play_authorized = true;
  facts.set_piece_active = false;
  SimulationAccess::BallOf(simulation)->ResetSituation(Vector3(50.5f, 0.0f, 0.0f));
  football::test::RuleCommandProbe commands;
  SimulationAccess::RulesOf(simulation).Process(
      facts, SimulationAccess::OptionsOf(simulation),
      SimulationAccess::RngOf(simulation), commands);
  REQUIRE(!commands.calls.empty());
  REQUIRE(commands.calls.front() == "stop");
}

}  // namespace
