// Bounded runtime harness for the simulation composition.
//
// It builds a real Simulation, runs a handful of ticks and tears it down. It is
// deliberately NOT a match simulation: it never drives a match to half time or
// full time and asserts no physics outcome.
//
// Its job is to catch composition and lifecycle errors that the leaf unit tests
// cannot observe at all -- for example a Player holding a dangling
// PlayerDefinition reference (which reads out as height 0 / garbage id). Every
// phase is timed, and the CTest wall-clock timeout is the real guard: bounding
// the tick count cannot catch a single Step that never returns.
//
// Internals (rosters, teams, clock) are reached through the internal
// testing::SimulationAccess escape hatch; this is test-only code and is never
// linked into the product.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_control_set.hpp"
#include "sim/player/player_definition.hpp"
#include "sim/observation/world_state.hpp"
#include "sim/runtime/match_config.hpp"
#include "sim/simulation.hpp"
#include "sim/simulation_config.hpp"
#include "sim/team/team.hpp"
#include "sim/testing/simulation_access.hpp"

namespace {

using football::sim::testing::SimulationAccess;
using Clock = std::chrono::steady_clock;

double SecondsSince(const Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool IsFinite(const football::math::Vector3& v) {
  return std::isfinite(v.coords[0]) && std::isfinite(v.coords[1]) &&
         std::isfinite(v.coords[2]);
}

// A minimal but complete roster: explicit identities, an 11-strong tactical
// formation, and per-player heights so a dangling PlayerDefinition reads as
// height 0 instead of silently passing.
football::sim::TeamSetup MakeTeam(const char* name,
                                 football::player::PlayerId first_id) {
  football::sim::TeamSetup team;
  team.name = name;
  for (int i = 0; i < 11; ++i) {
    football::player::PlayerDefinition player;
    player.id = first_id + static_cast<football::player::PlayerId>(i);
    player.database_id = 100 + i;
    player.height = 1.70f + 0.01f * static_cast<float>(i);
    team.players.push_back(std::move(player));
  }
  team.tactical_formation = {
      {{-1.0f, 0.0f}, e_PlayerRole_GK},  {{-0.7f, 0.75f}, e_PlayerRole_LB},
      {{-1.0f, 0.25f}, e_PlayerRole_CB}, {{-1.0f, -0.25f}, e_PlayerRole_CB},
      {{-0.7f, -0.75f}, e_PlayerRole_RB}, {{0.0f, 0.5f}, e_PlayerRole_CM},
      {{-0.2f, 0.0f}, e_PlayerRole_CM},  {{0.0f, -0.5f}, e_PlayerRole_CM},
      {{0.6f, 0.75f}, e_PlayerRole_LM},  {{1.0f, 0.0f}, e_PlayerRole_CF},
      {{0.6f, -0.75f}, e_PlayerRole_RM}};
  return team;
}

void InitMatch(Simulation& sim) {
  sim.Init(MakeTeam("home", 0), MakeTeam("away", 11), football::pitch::Pitch{},
           MatchOptions{}, football::ball::BallModel{});
}

}  // namespace

TEST_CASE("Runtime harness: two rosters initialise with live definitions",
          "[runtime][sim]") {
  Simulation sim;
  const auto init_start = Clock::now();
  InitMatch(sim);
  std::fprintf(stderr, "[runtime] Init: %.3fs\n", SecondsSince(init_start));

  REQUIRE(SimulationAccess::IsInitializedOf(sim));
  Team* home = SimulationAccess::TeamOf(sim, 0);
  Team* away = SimulationAccess::TeamOf(sim, 1);
  REQUIRE(home != nullptr);
  REQUIRE(away != nullptr);

  const std::span<Player* const> home_players = home->GetAllPlayers();
  const std::span<Player* const> away_players = away->GetAllPlayers();
  REQUIRE(home_players.size() == 11);
  REQUIRE(away_players.size() == 11);
  CHECK(home->GetActivePlayersCount() == 11);
  CHECK(away->GetActivePlayersCount() == 11);

  for (std::size_t i = 0; i < home_players.size(); ++i) {
    // Roster order, identity and side survive construction.
    CHECK(home_players[i]->GetID() == static_cast<football::player::PlayerId>(i));
    CHECK(away_players[i]->GetID() == static_cast<football::player::PlayerId>(11 + i));
    CHECK(home_players[i]->GetTeamSide() == football::sim::TeamSide::Home);
    CHECK(away_players[i]->GetTeamSide() == football::sim::TeamSide::Away);
    // Player holds a const PlayerDefinition&: a dead reference reads as zero.
    CHECK(home_players[i]->GetModel().height > 1.0f);
    CHECK(home_players[i]->GetModel().id == home_players[i]->GetID());
    CHECK(IsFinite(home_players[i]->GetPosition()));
  }

  const auto stop_start = Clock::now();
  CHECK(sim.Stop());
  std::fprintf(stderr, "[runtime] Stop: %.3fs\n", SecondsSince(stop_start));
  CHECK_FALSE(sim.Stop());  // Idempotent: reports already stopped.
}

TEST_CASE("Runtime harness: initial team orientation is Simulation-owned",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Team* home = SimulationAccess::TeamOf(sim, 0);
  Team* away = SimulationAccess::TeamOf(sim, 1);
  REQUIRE(home != nullptr);
  REQUIRE(away != nullptr);
  // Historical initial orientation: both teams start in the first processing
  // frame (dynamic -1); only the persistent direction differs by slot. Team
  // keeps no copy of this -- Simulation owns the only TeamFrame.
  CHECK(home->GetDynamicSide() == -1);
  CHECK(away->GetDynamicSide() == -1);
  CHECK(home->GetStaticSide() == -1);
  CHECK(away->GetStaticSide() == 1);
  CHECK_FALSE(home->isMirrored());
  CHECK_FALSE(away->isMirrored());
  CHECK(home->onOriginalSide());
  CHECK_FALSE(away->onOriginalSide());
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a few ticks advance and tear down cleanly",
          "[runtime][sim]") {
  Simulation sim;
  const auto init_start = Clock::now();
  InitMatch(sim);
  std::fprintf(stderr, "[runtime] Init: %.3fs\n", SecondsSince(init_start));

  PlayerControlSet controls;
  const WorldState before = sim.Observe();
  const auto tick_start = Clock::now();
  for (int tick = 0; tick < 5; ++tick) {
    sim.Step(controls);
    std::fprintf(stderr, "[runtime] Step %d: cumulative %.3fs\n", tick + 1,
                 SecondsSince(tick_start));
  }

  const WorldState world = sim.Observe();
  // The timeline must advance, otherwise "it did not crash" would hide a
  // stalled Step loop.
  CHECK(world.tick > before.tick);
  CHECK(world.players.size() == 22);
  CHECK(IsFinite(world.ball_position));
  int active = 0;
  for (const WorldPlayerState& player : world.players) {
    CHECK(player.id != football::player::kInvalidPlayerId);
    CHECK(IsFinite(player.position));
    if (player.active) ++active;
  }
  CHECK(active == 22);

  const auto stop_start = Clock::now();
  CHECK(sim.Stop());
  std::fprintf(stderr, "[runtime] Stop: %.3fs\n", SecondsSince(stop_start));
}

TEST_CASE("Runtime harness: Mirror twice restores players, sides and ball",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Team* home = SimulationAccess::TeamOf(sim, 0);
  REQUIRE(home != nullptr);

  const WorldState before = sim.Observe();
  const int dynamic_before = home->GetDynamicSide();
  std::vector<int> player_sides;
  for (Player* player : home->GetAllPlayers()) player_sides.push_back(player->GetDynamicSide());

  // Mirror both teams but not the ball; mirroring is its own inverse.
  sim.Mirror(true, true, false);
  CHECK(home->GetDynamicSide() == -dynamic_before);
  for (Player* player : home->GetAllPlayers()) {
    CHECK(player->GetDynamicSide() == -dynamic_before);
  }
  sim.Mirror(true, true, false);

  CHECK(home->GetDynamicSide() == dynamic_before);
  const WorldState after = sim.Observe();
  REQUIRE(after.players.size() == before.players.size());
  for (std::size_t i = 0; i < before.players.size(); ++i) {
    CHECK(after.players[i].id == before.players[i].id);
    CHECK(after.players[i].position.coords[0] == Catch::Approx(before.players[i].position.coords[0]));
    CHECK(after.players[i].position.coords[1] == Catch::Approx(before.players[i].position.coords[1]));
  }
  CHECK(after.ball_position.coords[0] == Catch::Approx(before.ball_position.coords[0]));
  CHECK(after.ball_position.coords[1] == Catch::Approx(before.ball_position.coords[1]));
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: SwitchEnds flips both directions, not the tick frame",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Team* home = SimulationAccess::TeamOf(sim, 0);
  REQUIRE(home != nullptr);

  const int dynamic_before = home->GetDynamicSide();
  const int static_before = home->GetStaticSide();
  const bool mirrored_before = home->isMirrored();

  // Half-time change of ends: both directions flip; the canonical between-tick
  // frame (mirrored) must not change.
  SimulationAccess::SwitchTeamEnds(sim, 0);
  CHECK(home->GetDynamicSide() == -dynamic_before);
  CHECK(home->GetStaticSide() == -static_before);
  CHECK(home->isMirrored() == mirrored_before);
  // Publication is part of the behaviour: a switched end that skipped it would
  // leave every player reading the previous tick's team facts.
  for (Player* player : home->GetAllPlayers()) {
    CHECK(player->GetDynamicSide() == -dynamic_before);
    CHECK(player->GetStaticSide() == -static_before);
  }

  SimulationAccess::SwitchTeamEnds(sim, 0);
  CHECK(home->GetDynamicSide() == dynamic_before);
  CHECK(home->GetStaticSide() == static_before);
  CHECK(home->isMirrored() == mirrored_before);
  CHECK(sim.Stop());
}
