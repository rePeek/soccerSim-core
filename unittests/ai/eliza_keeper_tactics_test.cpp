// P21.5 acceptance: keeper positioning/rush/save intent, and the Coach ->
// TacticalBoard -> Player AI split (changing the board changes the decision).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>

#include "ai/eliza/defense.hpp"
#include "ai/eliza/eliza_observation.hpp"
#include "ai/eliza/keeper.hpp"
#include "ai/eliza/off_ball.hpp"
#include "eliza_test_world.hpp"
#include "football/ai/tactical_board.hpp"

namespace {
using football::math::Vector3;
using football::sim::TeamSide;
using football::ai::test::kPlayers;
using football::ai::test::MakeTeam;
using football::ai::test::MakeWorld;

}  // namespace

TEST_CASE("Eliza keeper holds the box and advances toward the ball", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  const WorldPlayerState* keeper =
      football::ai::eliza::FindPlayer(world, home.players[0].id);
  REQUIRE(keeper != nullptr);
  const float goal_x = -world.pitch.half_length();

  // Ball at the centre: the keeper stays on the goal line area.
  world.ball_position = Vector3(0.f, 0.f, 0.11f);
  const football::ai::eliza::KeeperDecision centre = football::ai::eliza::DecideKeeper(
      world, *keeper, football::ai::eliza::kElizaParameters);
  CHECK(centre.target.coords[0] >= goal_x);
  CHECK(centre.target.coords[0] <= goal_x + 16.4f);
  CHECK(std::fabs(centre.target.coords[1]) <= 20.05f);
  CHECK_FALSE(centre.rush);

  // Ball near the box: the keeper comes out but stays inside its box.
  world.ball_position = Vector3(goal_x + 8.f, 0.f, 0.11f);
  const football::ai::eliza::KeeperDecision near = football::ai::eliza::DecideKeeper(
      world, *keeper, football::ai::eliza::kElizaParameters);
  CHECK(near.target.coords[0] >= goal_x);
  CHECK(near.target.coords[0] <= goal_x + 16.4f);
  CHECK(std::fabs(near.target.coords[1]) <= 20.05f);
}

TEST_CASE("Eliza keeper asks for a save on a close loose ball", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  WorldPlayerState& keeper = world.players[0];
  const float goal_x = -world.pitch.half_length();
  keeper.position = Vector3(goal_x + 2.f, 0.f, 0.f);
  world.ball_position = Vector3(goal_x + 3.f, 0.f, 0.5f);

  const football::ai::eliza::KeeperDecision close = football::ai::eliza::DecideKeeper(
      world, keeper, football::ai::eliza::kElizaParameters);
  CHECK(close.save);

  // Retained ball: the keeper no longer asks for a save.
  world.ball_retainer = away.players[9].id;
  const football::ai::eliza::KeeperDecision retained = football::ai::eliza::DecideKeeper(
      world, keeper, football::ai::eliza::kElizaParameters);
  CHECK_FALSE(retained.save);

  // Far away: no save.
  world.ball_retainer.reset();
  world.ball_position = Vector3(goal_x + 30.f, 0.f, 0.5f);
  const football::ai::eliza::KeeperDecision far = football::ai::eliza::DecideKeeper(
      world, keeper, football::ai::eliza::kElizaParameters);
  CHECK_FALSE(far.save);
}

TEST_CASE("Eliza keeper emits a Save action through the policy", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  const float goal_x = -world.pitch.half_length();
  for (WorldPlayerState& player : world.players) player.position = Vector3(0.f, 0.f, 0.f);
  world.players[0].position = Vector3(goal_x + 2.f, 0.f, 0.f);
  world.ball_position = Vector3(goal_x + 3.f, 0.f, 0.5f);
  const PlayerControlSet output = football::ai::test::RunEliza(home, away, world);
  const PlayerControl* keeper = output.Get(home.players[0].id);
  REQUIRE(keeper != nullptr);
  CHECK(keeper->action == ControlAction::Save);
}

TEST_CASE("TacticalPlan changes the interpreted shape", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  world.teams[0].has_possession = true;
  world.teams[0].fading_possession_amount = 1.4f;
  const football::pitch::Pitch pitch;
  const football::ai::TacticalBoard home_board =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, pitch);
  const WorldPlayerState* winger =
      football::ai::eliza::FindPlayer(world, home.players[6].id);
  REQUIRE(winger != nullptr);

  auto anchor_at = [&](float width) {
    football::ai::TacticalBoard board = home_board;
    board.plan.in_possession.attacking_width = width;
    const football::ai::TacticalBoard boards[2] = {board, board};
    std::array<football::ai::SlotAssignment, 2> assignments;
    for (unsigned side = 0; side < 2; ++side)
      assignments[side] =
          football::ai::DefaultSlotAssignment(world, static_cast<TeamSide>(side));
    const auto interpretation = football::ai::TacticalInterpreter::Build(
        boards, world, assignments,
        football::ai::InterpretationState::FromWorld(world));
    return football::ai::eliza::CalculateFormationAnchor(
        world, football::ai::FindEffective(interpretation.players[0], winger->id),
        *winger);
  };

  const Vector3 narrow = anchor_at(0.2f);
  const Vector3 wide = anchor_at(0.95f);
  CHECK(std::fabs(wide.coords[1]) > std::fabs(narrow.coords[1]));

  football::ai::TacticalBoard board = home_board;
  const football::ai::TacticalBoard boards[2] = {board, board};
  std::array<football::ai::SlotAssignment, 2> assignments;
  for (unsigned side = 0; side < 2; ++side)
    assignments[side] =
        football::ai::DefaultSlotAssignment(world, static_cast<TeamSide>(side));
  const auto interpretation = football::ai::TacticalInterpreter::Build(
      boards, world, assignments, football::ai::InterpretationState::FromWorld(world));
  const Vector3 deep = football::ai::eliza::CalculateOffBallTarget(
      world, football::ai::FindEffective(interpretation.players[0], winger->id),
      *winger, football::ai::eliza::kElizaParameters);
  CHECK(std::isfinite(deep.coords[0]));
  CHECK(std::isfinite(deep.coords[1]));
}

TEST_CASE("TacticalBoard marking target selects the defended opponent", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  world.teams[0].fading_possession_amount = 0.6f;
  const WorldPlayerState* defender =
      football::ai::eliza::FindPlayer(world, home.players[2].id);
  REQUIRE(defender != nullptr);

  football::ai::EffectivePlayerTactics effective;
  effective.player = defender->id;
  effective.assigned = true;
  const football::player::PlayerId marked = away.players[9].id;
  effective.mark_target = marked;
  const WorldPlayerState* target =
      football::ai::eliza::SelectMarkingTarget(world, *defender, &effective);
  REQUIRE(target != nullptr);
  CHECK(target->id == marked);

  // Without a directive it falls back to the opponent nearest the ball.
  const WorldPlayerState* nearest = football::ai::eliza::FindPlayer(world, away.players[7].id);
  REQUIRE(nearest != nullptr);
  world.ball_position = nearest->position;
  const WorldPlayerState* fallback =
      football::ai::eliza::SelectMarkingTarget(world, *defender, nullptr);
  REQUIRE(fallback != nullptr);
  CHECK(fallback->side == TeamSide::Away);
}
