#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "rule_command_fixture.hpp"
#include "sim/event/event_log.hpp"
#include "sim/event/match_event.hpp"
#include "sim/referee/ruling.hpp"
#include "sim/referee/referee_view.hpp"
#include "sim/simulation.hpp"
#include "sim/testing/simulation_access.hpp"

namespace {

using football::sim::Tick;
using football::sim::testing::SimulationAccess;
using blunted::Vector3;

namespace event = football::sim::event;
namespace rules = football::sim::rules;


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

TEST_CASE("referee turns a crossed goal mouth into an award ruling for the opponent",
          "[sim][event][referee]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  auto& referee = SimulationAccess::RulesOf(simulation);
  // Establish the canonical dynamic sides (home -1, away +1) without a full Step.
  SimulationAccess::TeamOf(simulation, 1)->Mirror();
  const auto tick = SimulationAccess::RefereeFactsOf(simulation);
  football::test::RuleCommandProbe commands;
  RulingRecorder rulings;

  // Crossing the home side's goal line credits the away side.
  referee.GoalMouthCrossed(tick.home.GetDynamicSide(), tick, &rulings);

  REQUIRE(rulings.rulings.size() == 1);
  const auto* goal = std::get_if<event::AwardGoalRuling>(&rulings.rulings.front());
  REQUIRE(goal != nullptr);
  REQUIRE(goal->team == tick.away.GetTeamSide());
  REQUIRE(commands.calls.empty());  // A goal verdict is not a synchronous stop command.
}

TEST_CASE("referee classifies a foul from frozen assessment evidence",
          "[sim][event][referee]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  // A fresh referee with no pending restart so the trip gate is open.
  struct ProbeReferee : Referee {
    using Referee::Referee;
    using Referee::buffer;
  };
  ProbeReferee referee(
      *SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation)),
      SimulationAccess::OptionsOf(simulation).ball_position);
  referee.buffer.active = false;
  auto* victim = SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  auto* offender = SimulationAccess::TeamOf(simulation, 1)->GetAllPlayers()[1];

  // A standing-contact fall is only a foul when the frozen evidence matches
  // the legacy conditions: possession, action type, distance and opponent.
  football::sim::FoulAssessment fact;
  fact.kind = football::sim::FoulKind::StandingFall;
  fact.victim = victim->GetID();
  fact.offender = offender->GetID();
  fact.victim_team_id = victim->GetTeam()->GetID();
  fact.offender_team_id = offender->GetTeam()->GetID();
  fact.victim_team_fading_possession = 1.2f;
  fact.offender_action_type = static_cast<int>(e_FunctionType_Interfere);
  fact.victim_position = victim->GetPosition();
  fact.position = victim->GetPitchPosition();
  fact.ball_position = victim->GetPosition();

  referee.AssessFoul(victim, offender, fact, Tick{42});
  REQUIRE(referee.GetCurrentFoulType() == 1);
  REQUIRE(referee.GetCurrentFoulPlayer() == offender);
}

TEST_CASE("the TripNotice test adapter still reports falls without a foul",
          "[sim][event][referee]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  auto& referee = SimulationAccess::RulesOf(simulation);
  auto* victim = SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  auto* offender = SimulationAccess::TeamOf(simulation, 1)->GetAllPlayers()[1];
  referee.TripNotice(victim, offender, 1, Tick{123}, Vector3(0));
  REQUIRE(referee.GetCurrentFoulType() == 0);  // A little standing trip is not a foul.
}

}  // namespace
