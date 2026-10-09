#include <algorithm>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/simulation.hpp"
#include "sim/team/team.hpp"
#include "sim/testing/simulation_access.hpp"

namespace {
using football::sim::testing::SimulationAccess;
using football::sim::Tick;
using football::sim::TickSpan;

void RequireLatest(Simulation& simulation, std::uint64_t steps) {
  const auto world = simulation.Observe();
  const auto& metadata = simulation.SnapshotMetadata();
  const auto* record = simulation.Snapshots().Latest();
  REQUIRE(record != nullptr);
  REQUIRE(record->stamp.step_index == steps);
  REQUIRE(record->stamp.timeline_tick == Tick{world.tick});
  REQUIRE(record->stamp.generation == world.reset_sequence);
  REQUIRE(record->snapshot.ball.velocity == world.ball_velocity);
  REQUIRE(record->snapshot.players.size() == world.players.size());
  REQUIRE(metadata.player_ids.size() == world.players.size());
  REQUIRE(metadata.pitch == world.pitch);
  REQUIRE(metadata.animation_library_hash != 0);
  const auto ball_frame = ToHomePitchFrame(*SimulationAccess::TeamOf(simulation,
      SimulationAccess::OptionsOf(simulation).reverse_team_processing ? 1 : 0));
  REQUIRE(record->snapshot.ball.angular_velocity == ball_frame.Direction(
      SimulationAccess::BallOf(simulation)->state().angular_velocity));
  // WorldState keeps its legacy cached Predict(0), which can lag true position.
  // Snapshot must use authoritative BallState without refreshing that cache.
  REQUIRE(record->snapshot.ball.position == ball_frame.Position(
      SimulationAccess::BallOf(simulation)->state().position));
  std::size_t index = 0;
  for (int team = 0; team < 2; ++team) {
    for (auto* actor : SimulationAccess::TeamOf(simulation, team)->GetAllPlayers()) {
      const auto& observed = record->snapshot.players[index];
      const auto& expected = world.players[index];
      REQUIRE(metadata.player_ids[index] == actor->GetID());
      REQUIRE(metadata.player_ids[index] == expected.id);
      REQUIRE(observed.position == expected.position);
      REQUIRE(observed.velocity == expected.velocity);
      REQUIRE(observed.facing == expected.facing);
      REQUIRE(observed.body_facing == ToHomePitchFrame(*actor->GetTeam()).Direction(
          actor->GetKinematicState().bodyFacing));
      REQUIRE(observed.active == expected.active);
      REQUIRE(observed.has_possession == expected.has_possession);
      if (observed.active && actor->GetCurrentAnim()) {
        REQUIRE(observed.animation_id == actor->GetCurrentAnim()->animationId);
        REQUIRE(observed.frame == static_cast<std::uint32_t>(actor->GetCurrentFrame()));
      } else {
        REQUIRE(observed.animation_id == -1);
        REQUIRE(observed.frame == 0);
      }
      ++index;
    }
  }
}
}

TEST_CASE("Snapshot samples initial ceremonies both halves and terminal steps exactly once",
          "[sim][snapshot]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options;
    options.reverse_team_processing = reverse;
    options.half_duration = TickSpan{100};
    options.snapshot_capacity = 3000;
    const auto home = football::app::fixtures::MakeDefaultHomeTeam();
    const auto away = football::app::fixtures::MakeDefaultAwayTeam();
    simulation.Init(home, away, {}, options);
    REQUIRE(simulation.Snapshots().Size() == 1);
    RequireLatest(simulation, 0);
    const auto metadata = simulation.SnapshotMetadata();
    auto policy = football::test::MakeDefaultAI(simulation);
    std::uint64_t steps = 0;
    bool second_half = false;
    bool ceremony = false;
    while (!simulation.Finished()) {
      const auto before = simulation.Observe();
      football::test::StepDefaultAI(simulation, policy);
      ++steps;
      REQUIRE(steps < 2500);
      RequireLatest(simulation, steps);
      REQUIRE(simulation.Snapshots().Size() == steps + 1);
      REQUIRE(simulation.SnapshotMetadata().player_ids == metadata.player_ids);
      second_half |= simulation.Observe().phase == MatchPhase::SecondHalf;
      ceremony |= !before.in_play;
    }
    REQUIRE(second_half);
    REQUIRE(ceremony);
    REQUIRE(simulation.Result().duration_ticks == steps);
    // The terminal transition counts an executed step without advancing timeline.
    REQUIRE(simulation.Snapshots().FindStep(steps - 1)->stamp.timeline_tick ==
            simulation.Snapshots().Latest()->stamp.timeline_tick);
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    simulation.Step({});
    RequireLatest(simulation, steps);
    REQUIRE(simulation.Snapshots().Size() == steps + 1);
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
    simulation.Stop();
    REQUIRE(simulation.Snapshots().Empty());
    REQUIRE(simulation.SnapshotMetadata().player_ids.empty());
    REQUIRE_THROWS_AS(simulation.Step({}), std::logic_error);
    simulation.Init(home, away, {}, options);
    RequireLatest(simulation, 0);
    REQUIRE(simulation.Snapshots().Size() == 1);
  }
}

TEST_CASE("Snapshot history survives resets time jumps send-offs and unequal rosters",
          "[sim][snapshot]") {
  Simulation simulation;
  MatchOptions options;
  options.snapshot_capacity = 4;
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  home.formation.resize(3);
  away.formation.resize(2);
  simulation.Init(home, away, {}, options);
  const auto slots = simulation.SnapshotMetadata().player_ids;
  RequireLatest(simulation, 0);
  // Only instantiated runtime players have slots; omitted profiles are not actors.
  REQUIRE(slots.size() == 5);
  simulation.Step({});
  RequireLatest(simulation, 1);
  const auto old = *simulation.Snapshots().Latest();
  simulation.ResetSituation(blunted::Vector3(1, 2, 0));
  simulation.ResetSituation(blunted::Vector3(3, 4, 0));
  REQUIRE(simulation.Snapshots().Latest()->stamp.generation == old.stamp.generation);
  REQUIRE(simulation.Snapshots().FindStep(1) != nullptr);
  simulation.AdvanceTime(TickSpan{1000});
  REQUIRE(simulation.Snapshots().Size() == 2);
  REQUIRE(simulation.Snapshots().Find(Tick{500}, 2) == nullptr);
  auto* actor = SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  actor->SendOff(*SimulationAccess::BallOf(simulation), Tick{simulation.Observe().tick},
                SimulationAccess::RngOf(simulation));
  simulation.Step({});
  RequireLatest(simulation, 2);
  REQUIRE(simulation.Snapshots().Latest()->stamp.generation >= old.stamp.generation + 2);
  REQUIRE_FALSE(simulation.Snapshots().Latest()->snapshot.players[1].active);
  REQUIRE(simulation.SnapshotMetadata().player_ids == slots);
  for (std::uint64_t step = 3; step <= 10; ++step) {
    simulation.Step({});
    RequireLatest(simulation, step);
  }
  REQUIRE(simulation.Snapshots().Size() == 4);
  REQUIRE(simulation.Snapshots().Oldest()->stamp.step_index == 7);
  REQUIRE(simulation.Snapshots().FindStep(6) == nullptr);
}

TEST_CASE("invalid snapshot capacity is rejected before startup or RNG draws", "[sim][snapshot]") {
  Simulation simulation;
  MatchOptions options;
  options.snapshot_capacity = 0;
  const auto rng = SimulationAccess::RngOf(simulation).engine();
  REQUIRE_THROWS_AS(simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), {}, options), std::invalid_argument);
  REQUIRE(simulation.Snapshots().Empty());
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
  REQUIRE_FALSE(simulation.Finished());
}
