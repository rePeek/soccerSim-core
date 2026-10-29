// TEAM-2 acceptance: Team is the sole owner of its Player entities. These
// tests exercise the live aggregate directly (ownership, order, lifecycle) and
// through Simulation (TeamView borrowing, send-off retention, re-Init), without
// running a match.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "football/animation/library.hpp"
#include "football/pitch/pitch.hpp"
#include "football/team/team.hpp"
#include "player/player.hpp"
#include "testing/simulation_access.hpp"

using namespace football::sim;
using football::sim::testing::SimulationAccess;

namespace {

TeamSetup TwoPlayerSetup(unsigned first) {
  TeamSetup setup;
  setup.name = "Team " + std::to_string(first);
  for (unsigned i = 0; i < 2; ++i) {
    football::player::PlayerDefinition player;
    player.id = first + i;
    player.height = 1.75f + i * .1f;
    setup.players.push_back(player);
    setup.formation.push_back({{-.7f + i * .4f, i * .3f},
        i == 0 ? e_PlayerRole_GK : e_PlayerRole_CF});
  }
  return setup;
}

std::unique_ptr<Player> MakeTestPlayer(football::player::PlayerId id,
                                       const AnimationLibrary& animations,
                                       const football::pitch::Pitch& pitch) {
  football::player::PlayerDefinition definition;
  definition.id = id;
  definition.height = 1.8f;
  return std::make_unique<Player>(0, TeamSide::Home, definition,
                                  animations, pitch);
}

}  // namespace

TEST_CASE("Team owns a Player after the caller releases it", "[team][ownership]") {
  AnimationLibrary animations;
  const football::pitch::Pitch pitch;
  football::team::Team team{football::team::TeamDefinition{}};
  auto actor = MakeTestPlayer(77, animations, pitch);
  Player* raw = actor.get();
  team.AddPlayer(std::move(actor));
  CHECK_FALSE(actor);
  REQUIRE(team.Players().size() == 1);
  CHECK(team.Players()[0] == raw);
  CHECK(team.Players()[0]->GetID() == 77);
  // A fresh Player is inactive until activated.
  CHECK(team.ActivePlayerCount() == 0);
  team.ClearPlayers();
  CHECK(team.Players().empty());
}

TEST_CASE("Team rejects a null Player without corrupting its ordered view",
          "[team][ownership]") {
  football::team::Team team{football::team::TeamDefinition{}};
  CHECK_THROWS_AS(team.AddPlayer(nullptr), std::invalid_argument);
  CHECK(team.Players().empty());
  CHECK(team.ActivePlayerCount() == 0);
}

TEST_CASE("Team traversal keeps declaration order and retains sent-off Players",
          "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  sim.Init(TwoPlayerSetup(50), TwoPlayerSetup(500), football::pitch::Pitch{},
           options);
  auto& home = SimulationAccess::TeamOf(sim, 0);
  const auto players = home.Players();
  REQUIRE(players.size() == 2);
  CHECK(players[0]->GetID() == 50);
  CHECK(players[1]->GetID() == 51);
  CHECK(SimulationAccess::TeamStateOf(sim, 0).goalie == players[0]);
  CHECK(home.ActivePlayerCount() == 2);

  // Send-off vacates the live slot and drops the active count, but the entity
  // stays owned by the Team in the same position.
  SimulationAccess::SendOff(sim, *players[0]);
  CHECK_FALSE(players[0]->IsActive());
  CHECK(home.ActivePlayerCount() == 1);
  const auto after = SimulationAccess::TeamOf(sim, 0).Players();
  REQUIRE(after.size() == 2);
  CHECK(after[0] == players[0]);
  CHECK(after[1] == players[1]);
  // The goalkeeper role hands over to the first remaining active player.
  CHECK(SimulationAccess::TeamStateOf(sim, 0).goalie == players[1]);
  const auto world = sim.Observe();
  REQUIRE(world.active_lineups);
  CHECK_FALSE((*world.active_lineups)[0].slots[0]);
  CHECK((*world.active_lineups)[0].slots[1] == 51);
  CHECK(sim.Stop());
}

TEST_CASE("TeamView borrows the Team list and snapshots keep its order",
          "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  sim.Init(TwoPlayerSetup(50), TwoPlayerSetup(500), football::pitch::Pitch{},
           options);
  const auto home = SimulationAccess::TeamOf(sim, 0).Players();
  const auto away = SimulationAccess::TeamOf(sim, 1).Players();
  const auto home_view = SimulationAccess::TeamViewOf(sim, 0);
  const auto away_view = SimulationAccess::TeamViewOf(sim, 1);
  // No copied list: the view points into the Team's own ordered container.
  CHECK(home_view.players.data() == home.data());
  CHECK(home_view.players.size() == home.size());
  CHECK(away_view.players.data() == away.data());
  CHECK(home_view.goalie == SimulationAccess::TeamStateOf(sim, 0).goalie);

  // SnapshotMetadata uses the same home-then-away declaration order.
  const auto& metadata = sim.SnapshotMetadata();
  REQUIRE(metadata.player_ids.size() == 4);
  CHECK(metadata.player_ids[0] == 50);
  CHECK(metadata.player_ids[1] == 51);
  CHECK(metadata.player_ids[2] == 500);
  CHECK(metadata.player_ids[3] == 501);
  CHECK(sim.PlayerSlots().team_sides[0] == TeamSide::Home);
  CHECK(sim.PlayerSlots().team_sides[1] == TeamSide::Home);
  CHECK(sim.PlayerSlots().team_sides[2] == TeamSide::Away);
  CHECK(sim.PlayerSlots().team_sides[3] == TeamSide::Away);
  CHECK(sim.Stop());
}

TEST_CASE("Stop destroys the owning Team and re-Init creates a fresh one",
          "[team][ownership][lifecycle]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  sim.Init(TwoPlayerSetup(50), TwoPlayerSetup(500), football::pitch::Pitch{},
           options);
  REQUIRE(sim.Stop());
  // Every borrow into the destroyed Team now fails cleanly.
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim, 0), std::logic_error);
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim, 1), std::logic_error);

  // A fresh composition owns a distinct set of entities.
  REQUIRE_NOTHROW(sim.Init(TwoPlayerSetup(80), TwoPlayerSetup(580),
                          football::pitch::Pitch{}, options));
  const auto players = SimulationAccess::TeamOf(sim, 0).Players();
  REQUIRE(players.size() == 2);
  CHECK(players[0]->GetID() == 80);
  CHECK(players[1]->GetID() == 81);
  // Safe to destroy with a fully live composition.
  REQUIRE_NOTHROW(sim.Stop());
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim, 0), std::logic_error);
}
