// TEAM-3A acceptance: the Team's declared SetPieceOrders select the restart
// taker in the Runtime RestartPlanner. Empty or ineligible orders must keep the
// legacy nearest-active-player selection, so default matches are unchanged.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "observation/pitch_frame.hpp"
#include "runtime/restart_planner.hpp"
#include "testing/simulation_access.hpp"

using namespace football::sim;
using football::sim::testing::SimulationAccess;

namespace {

TeamSetup RestartSetup(unsigned first) {
  TeamSetup setup;
  setup.name = "Team " + std::to_string(first);
  for (unsigned i = 0; i < 3; ++i) {
    football::player::PlayerDefinition player;
    player.id = first + i;
    player.height = 1.75f;
    setup.players.push_back(player);
    setup.formation.push_back({{-0.8f + i * 0.8f, 0.0f},
        i == 0 ? e_PlayerRole_GK : e_PlayerRole_CF});
  }
  return setup;
}

Vector3 HomeFramePosition(const Player* player) {
  return ToHomePitchFrame(player->GetTeamSide(), player->GetDynamicSide())
      .Position(player->GetPosition())
      .Get2D();
}

RestartPriority PriorityFor(const football::team::SetPieceOrders& orders,
                            e_GameMode mode, const Vector3& home_ball,
                            int attacking_side) {
  return RestartPriorityFor(orders, mode, home_ball.coords[1], attacking_side);
}

}  // namespace

TEST_CASE("Restart orders resolve to the declared priority per mode",
          "[team][restart]") {
  football::team::SetPieceOrders orders;
  orders.penalties = {1, 2};
  orders.direct_free_kicks = {3};
  orders.indirect_free_kicks = {4};
  orders.corners_left = {5};
  orders.corners_right = {6};
  orders.throw_ins = {7};

  const auto penalty = PriorityFor(orders, e_GameMode_Penalty, Vector3(0, 0, 0), 1);
  CHECK(penalty.primary.data() == orders.penalties.data());
  CHECK(penalty.secondary.empty());

  const auto free_kick =
      PriorityFor(orders, e_GameMode_FreeKick, Vector3(0, 0, 0), 1);
  CHECK(free_kick.primary.data() == orders.direct_free_kicks.data());
  CHECK(free_kick.secondary.data() == orders.indirect_free_kicks.data());

  // Corner side is team-relative: y on the team's attack side is its left.
  // Home attacks +X.
  CHECK(PriorityFor(orders, e_GameMode_Corner, Vector3(100, 34, 0), 1).primary.data() ==
        orders.corners_left.data());
  CHECK(PriorityFor(orders, e_GameMode_Corner, Vector3(100, -34, 0), 1).primary.data() ==
        orders.corners_right.data());
  // Away attacks -X: the same physical corner is its right.
  CHECK(PriorityFor(orders, e_GameMode_Corner, Vector3(100, 34, 0), -1).primary.data() ==
        orders.corners_right.data());
  CHECK(PriorityFor(orders, e_GameMode_Corner, Vector3(100, -34, 0), -1).primary.data() ==
        orders.corners_left.data());

  CHECK(PriorityFor(orders, e_GameMode_ThrowIn, Vector3(0, 0, 0), 1).primary.data() ==
        orders.throw_ins.data());
  CHECK(PriorityFor(orders, e_GameMode_KickOff, Vector3(0, 0, 0), 1).primary.empty());
  CHECK(PriorityFor(orders, e_GameMode_GoalKick, Vector3(0, 0, 0), 1).primary.empty());
}

TEST_CASE("Declared orders choose the restart taker and keep the fallback",
          "[team][restart]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  sim.Init(RestartSetup(50), RestartSetup(500), football::pitch::Pitch{}, options);
  const auto players = SimulationAccess::TeamOf(sim, 0).Players();
  REQUIRE(players.size() == 3);
  // The rules/planner view borrows the owning Team's static definition, which
  // is where the declared orders are read from.
  CHECK(SimulationAccess::TeamViewOf(sim, 0).definition ==
        &SimulationAccess::DefinitionOf(sim, TeamSide::Home));
  std::vector<Player*> active(players.begin(), players.end());
  const auto& pitch = SimulationAccess::PitchOf(sim);
  // Place the ball exactly on the third player so it is the nearest.
  const Vector3 ball = HomeFramePosition(players[2]);

  // No declared order: legacy nearest-active-player selection.
  const auto legacy = PlanRestart(pitch, ball, active, e_GameMode_ThrowIn, 0, {});
  CHECK(legacy.taker == players[2]);

  // A declared order names player 0 first; it is not the nearest but wins.
  football::team::SetPieceOrders orders;
  orders.throw_ins = {players[0]->GetID(), players[2]->GetID()};
  const auto declared = PlanRestart(
      pitch, ball, active, e_GameMode_ThrowIn, 0,
      PriorityFor(orders, e_GameMode_ThrowIn, ball, 1));
  CHECK(declared.taker == players[0]);

  // Declared taker absent/dismissed -> deterministic nearest fallback.
  orders.throw_ins = {9999};
  const auto fallback = PlanRestart(
      pitch, ball, active, e_GameMode_ThrowIn, 0,
      PriorityFor(orders, e_GameMode_ThrowIn, ball, 1));
  CHECK(fallback.taker == players[2]);

  // Sending off the declared first choice moves selection to the next name.
  orders.throw_ins = {players[0]->GetID(), players[1]->GetID()};
  SimulationAccess::SendOff(sim, *players[0]);
  std::vector<Player*> still_active;
  for (Player* player : players)
    if (player->IsActive()) still_active.push_back(player);
  const auto after_send_off = PlanRestart(
      pitch, ball, still_active, e_GameMode_ThrowIn, 0,
      PriorityFor(orders, e_GameMode_ThrowIn, ball, 1));
  CHECK(after_send_off.taker == players[1]);

  CHECK(sim.Stop());
}
