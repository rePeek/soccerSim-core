#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "rule_command_fixture.hpp"
#include "sim/event/event_log.hpp"
#include "sim/event/match_event.hpp"
#include "sim/event/referee_ruling.hpp"
#include "sim/event/simulation_fact.hpp"
#include "sim/event/tick_fact_buffer.hpp"
#include "sim/referee/referee_view.hpp"
#include "sim/simulation.hpp"
#include "sim/testing/simulation_access.hpp"

namespace {

using football::sim::Tick;
using football::sim::testing::SimulationAccess;
using blunted::Vector3;

namespace event = football::sim::event;
namespace rules = football::sim::rules;

TEST_CASE("tick fact buffer preserves emission order, identity and generation",
          "[sim][event][fact]") {
  event::TickFactBuffer buffer;
  buffer.BeginTick(Tick{5}, 7);
  REQUIRE_FALSE(buffer.HasPending());
  REQUIRE(buffer.empty());

  REQUIRE(buffer.Emit(event::BallTouchFact{}) == 0);
  REQUIRE(buffer.Emit(event::PlayerTripFact{}) == 1);
  REQUIRE(buffer.Emit(event::BallBoundaryFact{}) == 2);
  REQUIRE(buffer.pending_count() == 3);

  auto first = buffer.PopPending();
  REQUIRE(first.has_value());
  REQUIRE(first->tick == Tick{5});
  REQUIRE(first->generation == 7);
  REQUIRE(first->sequence == 0);
  REQUIRE(std::holds_alternative<event::BallTouchFact>(first->fact));

  auto second = buffer.PopPending();
  REQUIRE(second.has_value());
  REQUIRE(second->sequence == 1);
  REQUIRE(std::holds_alternative<event::PlayerTripFact>(second->fact));

  auto third = buffer.PopPending();
  REQUIRE(third.has_value());
  REQUIRE(third->sequence == 2);
  REQUIRE(std::holds_alternative<event::BallBoundaryFact>(third->fact));

  REQUIRE_FALSE(buffer.PopPending().has_value());
  REQUIRE_FALSE(buffer.HasPending());
  REQUIRE(buffer.pending_count() == 0);
  REQUIRE_FALSE(buffer.empty());  // Storage is reused until the next BeginTick.

  // A new tick drops the previous stream and restarts the sequence.
  buffer.BeginTick(Tick{6}, 8);
  REQUIRE(buffer.empty());
  REQUIRE_FALSE(buffer.HasPending());
  REQUIRE(buffer.Emit(event::BallBoundaryFact{}) == 0);
  REQUIRE(buffer.tick() == Tick{6});
  REQUIRE(buffer.generation() == 8);
}

TEST_CASE("event log stores only confirmed match history", "[sim][event][log]") {
  event::EventLog log;
  REQUIRE(log.empty());
  log.Record(event::GoalScoredEvent{Tick{10}, football::model::TeamSide::Home, 5u, false});
  log.Record(event::CardShownEvent{Tick{12}, football::model::TeamSide::Away, 6u, true});
  REQUIRE(log.size() == 2);
  REQUIRE(std::holds_alternative<event::GoalScoredEvent>(log.events().front()));
  REQUIRE(std::holds_alternative<event::CardShownEvent>(log.events().back()));
  log.Clear();
  REQUIRE(log.empty());
}

struct RulingRecorder final : event::RulingSink {
  std::vector<event::RefereeRuling> rulings;
  void Submit(const event::RefereeRuling& ruling) override { rulings.push_back(ruling); }
};

TEST_CASE("referee turns a goal-mouth fact into an award ruling for the opponent",
          "[sim][event][referee]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  auto& referee = SimulationAccess::RulesOf(simulation);
  // Establish the canonical dynamic sides (home -1, away +1) without a full Step.
  SimulationAccess::TeamOf(simulation, 1)->Mirror();
  const auto tick = SimulationAccess::RefereeFactsOf(simulation);
  std::vector<Player*> active;
  const rules::RefereeView view{tick, active, false};
  football::test::RuleCommandProbe commands;
  RulingRecorder rulings;

  event::StampedFact stamped;
  stamped.tick = tick.now;
  // Crossing the home side's goal line credits the away side.
  stamped.fact = event::BallBoundaryFact{
      event::BoundaryKind::GoalMouthCrossed, tick.home.GetDynamicSide(), Vector3(0), Vector3(0)};
  referee.Consume(stamped, view, commands, &rulings);

  REQUIRE(rulings.rulings.size() == 1);
  const auto* goal = std::get_if<event::AwardGoalRuling>(&rulings.rulings.front());
  REQUIRE(goal != nullptr);
  REQUIRE(goal->team == tick.away.GetTeamSide());
  REQUIRE(commands.calls.empty());  // A goal verdict is not a synchronous stop command.
}

TEST_CASE("player-trip facts reach the foul state through the write-only sink",
          "[sim][event][referee]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  auto& referee = SimulationAccess::RulesOf(simulation);
  auto* victim = SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  auto* offender = SimulationAccess::TeamOf(simulation, 1)->GetAllPlayers()[1];

  // The physical port only reports the fall; the referee still classifies it.
  referee.OnPlayerTripped(victim, offender, 1, Tick{123}, Vector3(0));
  REQUIRE(referee.GetCurrentFoulType() == 0);  // Types 1/2 never become fouls alone.
}

}  // namespace
