// P21.6 acceptance: the AI runs once per tick with a complete control set, its
// per-player memory is identity-keyed and resettable, and repeated intents
// remain plain intents (the Player's clock and arbitration are untouched).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "ai/eliza/eliza_policy.hpp"
#include "eliza_test_world.hpp"
#include "football/ai/tactical_board.hpp"

namespace {
using football::player::PlayerId;
using football::sim::TeamSide;
using football::ai::test::kPlayers;
using football::ai::test::MakeTeam;
using football::ai::test::MakeWorld;

std::array<football::ai::TacticalBoard, 2> MakeBoards(
    const football::sim::TeamSetup& home, const football::sim::TeamSetup& away) {
  const football::pitch::Pitch pitch;
  return {football::ai::MakeTacticalBoard(home, TeamSide::Home, pitch),
          football::ai::MakeTacticalBoard(away, TeamSide::Away, pitch)};
}

void Update(football::ai::ElizaPolicy& policy, const WorldState& world,
            std::array<football::ai::TacticalBoard, 2>& boards,
            PlayerControlSet& output) {
  football::ai::PlayerAIContext context;
  context.world = &world;
  context.tactics = boards;
  policy.Update(context, output);
}

bool SameControl(const PlayerControl& a, const PlayerControl& b) {
  return a.player == b.player && a.move_direction == b.move_direction &&
         a.desired_speed == b.desired_speed && a.look_at == b.look_at &&
         a.action == b.action && a.target_player == b.target_player &&
         a.target_position == b.target_position && a.power == b.power;
}
}  // namespace

TEST_CASE("Eliza possession memory is per PlayerId and Reset clears it", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  std::array<football::ai::TacticalBoard, 2> boards = MakeBoards(home, away);
  WorldState world = MakeWorld(home, away);
  const PlayerId carrier = home.players[5].id;
  const PlayerId teammate = home.players[6].id;
  world.ball_retainer = carrier;

  football::ai::ElizaPolicy policy;
  PlayerControlSet output;
  for (int tick = 1; tick <= 5; ++tick) {
    Update(policy, world, boards, output);
    CHECK(policy.PossessionTicks(carrier) == tick);
    CHECK(policy.PossessionTicks(teammate) == 0);
  }
  policy.Reset();
  CHECK(policy.PossessionTicks(carrier) == 0);
  Update(policy, world, boards, output);
  CHECK(policy.PossessionTicks(carrier) == 1);
}

TEST_CASE("Eliza possession memory clears when the ball moves on", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  std::array<football::ai::TacticalBoard, 2> boards = MakeBoards(home, away);
  WorldState world = MakeWorld(home, away);
  const PlayerId carrier = home.players[5].id;
  world.ball_retainer = carrier;

  football::ai::ElizaPolicy policy;
  PlayerControlSet output;
  Update(policy, world, boards, output);
  CHECK(policy.PossessionTicks(carrier) == 1);
  world.ball_retainer.reset();
  Update(policy, world, boards, output);
  CHECK(policy.PossessionTicks(carrier) == 0);
}

TEST_CASE("Eliza emits one complete control set per tick", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  std::array<football::ai::TacticalBoard, 2> boards = MakeBoards(home, away);
  const WorldState world = MakeWorld(home, away);

  football::ai::ElizaPolicy policy;
  PlayerControlSet output;
  Update(policy, world, boards, output);
  REQUIRE(output.controls().size() == static_cast<std::size_t>(kPlayers) * 2);
  std::vector<PlayerId> seen;
  for (const PlayerControl& control : output.controls()) {
    CHECK(std::find(seen.begin(), seen.end(), control.player) == seen.end());
    seen.push_back(control.player);
  }
}

TEST_CASE("Two fresh Eliza policies agree on the same world", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  std::array<football::ai::TacticalBoard, 2> boards = MakeBoards(home, away);
  WorldState world = MakeWorld(home, away);
  world.ball_retainer = home.players[5].id;
  world.ball_position = football::math::Vector3(0.f, 0.f, 0.11f);

  football::ai::ElizaPolicy first;
  football::ai::ElizaPolicy second;
  PlayerControlSet a;
  PlayerControlSet b;
  Update(first, world, boards, a);
  Update(second, world, boards, b);
  REQUIRE(a.controls().size() == b.controls().size());
  for (std::size_t i = 0; i < a.controls().size(); ++i)
    CHECK(SameControl(a.controls()[i], b.controls()[i]));
}
