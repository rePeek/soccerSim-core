// P21.5 acceptance: keeper positioning/rush/save intent, and the Coach ->
// TacticalBoard -> Player AI split (changing the board changes the decision).
#include <catch2/catch_test_macros.hpp>

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

const football::ai::PlayerDirective* DirectiveOf(const football::ai::TacticalBoard& board,
                                                 football::player::PlayerId id) {
  for (const football::ai::PlayerDirective& directive : board.players)
    if (directive.player == id) return &directive;
  return nullptr;
}
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

TEST_CASE("TacticalBoard changes the interpreted shape", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  world.teams[0].has_possession = true;
  world.teams[0].fading_possession_amount = 1.4f;
  const football::pitch::Pitch pitch;
  football::ai::TacticalBoard board =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, pitch);
  const WorldPlayerState* winger =
      football::ai::eliza::FindPlayer(world, home.players[6].id);
  REQUIRE(winger != nullptr);

  const Vector3 narrow = football::ai::eliza::CalculateFormationAnchor(
      world, board, DirectiveOf(board, winger->id), *winger);
  board.width *= 1.6f;
  const Vector3 wide = football::ai::eliza::CalculateFormationAnchor(
      world, board, DirectiveOf(board, winger->id), *winger);
  CHECK(std::fabs(wide.coords[1]) > std::fabs(narrow.coords[1]));

  board.depth = 0.9f;
  const Vector3 deep = football::ai::eliza::CalculateOffBallTarget(
      world, board, DirectiveOf(board, winger->id), *winger,
      football::ai::eliza::kElizaParameters);
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

  football::ai::PlayerDirective directive;
  directive.player = defender->id;
  directive.role = football::ai::PlannedPlayerRole::Defender;
  const football::player::PlayerId marked = away.players[9].id;
  directive.marking_target = marked;
  const WorldPlayerState* target =
      football::ai::eliza::SelectMarkingTarget(world, *defender, &directive);
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
