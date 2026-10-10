#include <numeric>
#include <catch2/catch_test_macros.hpp>
#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"

using football::sim::testing::SimulationAccess;
using blunted::Vector3;

TEST_CASE("body shadow preserves physics actors RNG and accepted facts under both frames",
          "[sim][body-shadow]") {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.snapshot_capacity = 4;
    options.reverse_team_processing = reverse;
    Simulation observed, control;
    observed.Init(home, away, football::model::Pitch{}, options);
    control.Init(home, away, football::model::Pitch{}, options);
    SimulationAccess::EnableBodyCollisionShadow(control, false);
    football::ai::DefaultAI policy(home, away, football::model::Pitch{});
    for (int step = 0; step < 800; ++step) {
      if (step == 400) {
        SimulationAccess::RequestChangeOfEnds(observed);
        SimulationAccess::RequestChangeOfEnds(control);
      }
      PlayerControlSet controls;
      policy.Update(observed.Observe(), controls);
      observed.Step(controls);
      control.Step(controls);
      REQUIRE(SimulationAccess::RngOf(observed).engine() == SimulationAccess::RngOf(control).engine());
      const auto& a = observed.Snapshots().Latest()->snapshot;
      const auto& b = control.Snapshots().Latest()->snapshot;
      REQUIRE(a.ball.position == b.ball.position);
      REQUIRE(a.ball.velocity == b.ball.velocity);
      REQUIRE(a.ball.angular_velocity == b.ball.angular_velocity);
      for (std::size_t i = 0; i < a.players.size(); ++i) {
        REQUIRE(a.players[i].position == b.players[i].position);
        REQUIRE(a.players[i].velocity == b.players[i].velocity);
        REQUIRE(a.players[i].animation_id == b.players[i].animation_id);
        REQUIRE(a.players[i].frame == b.players[i].frame);
        REQUIRE(a.players[i].has_possession == b.players[i].has_possession);
      }
      const auto& touches_a = SimulationAccess::RecordedTouchesOf(observed);
      const auto& touches_b = SimulationAccess::RecordedTouchesOf(control);
      REQUIRE(touches_a.size() == touches_b.size());
      if (!touches_a.empty()) {
        REQUIRE(touches_a.back().player == touches_b.back().player);
        REQUIRE(touches_a.back().type == touches_b.back().type);
        REQUIRE(touches_a.back().action_type == touches_b.back().action_type);
      }
    }
    const auto& report = SimulationAccess::BodyCollisionShadowReportOf(observed);
    REQUIRE(report.predicted_ticks > 0);
    REQUIRE(report.endpoint_errors.samples > 0);
    REQUIRE(report.endpoint_errors.samples == report.predicted_players);
    REQUIRE(std::accumulate(report.endpoint_errors.bins.begin(), report.endpoint_errors.bins.end(), 0ull)
            == report.endpoint_errors.samples);
    REQUIRE(report.matched_touches + report.missed_touches == report.accepted_accidental_touches);
    REQUIRE(SimulationAccess::BodyCollisionShadowReportOf(control).predicted_ticks == 0);
  }
}

TEST_CASE("fixed body slots and common collider coordinates survive end changes and sendoffs",
          "[sim][body-shadow]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.snapshot_capacity = 2;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, options);
    const auto owners = SimulationAccess::BodyColliderOwnersOf(simulation);
    for (int half = 0; half < 2; ++half) {
      if (half == 1) {
        SimulationAccess::RequestChangeOfEnds(simulation);
        simulation.Step({});
      }
      simulation.Mirror(reverse, !reverse, false);
      SimulationAccess::BeginBodyCollisionShadow(simulation);
      for (const auto& prediction : SimulationAccess::BodyShadowPredictionsOf(simulation)) {
        const auto id = prediction.colliders[0].id;
        REQUIRE(owners.at(id).first == prediction.player);
        const auto& capsule = std::get<football::ball::Capsule>(prediction.colliders[0].start);
        REQUIRE(capsule.a.Get2D() == prediction.start.position.Get2D());
        REQUIRE(prediction.end.position == prediction.start.position + prediction.start.velocity * football::sim::kTickSeconds);
      }
      simulation.Mirror(reverse, !reverse, false);
      REQUIRE(SimulationAccess::BodyColliderOwnersOf(simulation) == owners);
    }
    SimulationAccess::Deactivate(simulation, *SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers().back());
    REQUIRE(SimulationAccess::BodyColliderOwnersOf(simulation) == owners);
  }
}
