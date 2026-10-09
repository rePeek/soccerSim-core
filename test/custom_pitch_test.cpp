#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "app/fixtures/default_teams.hpp"
#include "football/ball/ball.hpp"
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

TEST_CASE("referee uses the injected sideline for throw-ins", "[sim][custom-pitch]") {
  const football::model::Pitch custom(100.0f, 64.0f, 0.04f, 1.6f, 0.025f);
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                  football::app::fixtures::MakeDefaultAwayTeam(), custom, {});
  auto facts = SimulationAccess::RefereeFactsOf(simulation);
  facts.phase = MatchPhase::FirstHalf;
  facts.now = football::sim::Tick{9001};
  facts.play_authorized = true;
  facts.set_piece_active = false;
  SimulationAccess::BallOf(simulation)->ResetSituation(Vector3(0.0f, 32.5f, 0.0f));
  football::test::RuleCommandProbe commands;
  SimulationAccess::RulesOf(simulation).Process(
      facts, SimulationAccess::OptionsOf(simulation),
      SimulationAccess::RngOf(simulation), commands);
  REQUIRE(!commands.calls.empty());
  REQUIRE(commands.calls.front() == "stop");
  const auto& buffer = SimulationAccess::RulesOf(simulation).GetBuffer();
  REQUIRE(buffer.desiredSetPiece == e_GameMode_ThrowIn);
  REQUIRE(std::fabs(buffer.restartPos.coords[1]) == 32.0f);
}

TEST_CASE("ball woodwork uses the injected goal post", "[sim][custom-pitch]") {
  const football::model::Pitch custom(100.0f, 64.0f, 0.04f, 1.6f, 0.025f);
  football::ball::Ball ball(football::model::BallConfig{}, custom);
  football::ball::BallState state;
  state.position = Vector3(custom.half_length() - 0.15f, custom.goal_half_width(), 0.1f);
  state.velocity = Vector3(3.0f, 0.0f, 0.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  ball.Reset(state);
  const auto next = ball.Predict(football::sim::TickSpan{1}, football::ball::BallEnvironment{});
  REQUIRE(next.velocity.coords[0] < 0.0f);
}

TEST_CASE("the same ball state diverges on different pitches", "[sim][custom-pitch]") {
  const football::model::Pitch short_grass(100.0f, 64.0f, 0.04f, 1.6f, 0.02f);
  const football::model::Pitch tall_grass(100.0f, 64.0f, 0.04f, 1.6f, 0.10f);
  football::ball::BallState state;
  state.position = Vector3(0.0f, 0.0f, 0.16f);
  state.velocity = Vector3(10.0f, 0.0f, 0.0f);
  state.angular_velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.orientation = blunted::Quaternion();
  football::ball::Ball short_ball(football::model::BallConfig{}, short_grass);
  football::ball::Ball tall_ball(football::model::BallConfig{}, tall_grass);
  short_ball.Reset(state);
  tall_ball.Reset(state);
  const auto short_next = short_ball.Predict(football::sim::TickSpan{1}, football::ball::BallEnvironment{});
  const auto tall_next = tall_ball.Predict(football::sim::TickSpan{1}, football::ball::BallEnvironment{});
  REQUIRE(tall_next.velocity.coords[0] < short_next.velocity.coords[0]);
}

}  // namespace
