#include <array>
#include <cmath>
#include <cstring>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "ai/default_ai.hpp"

namespace {
using blunted::Vector3;
using football::model::TeamSide;

WorldPlayerState Actor(unsigned id, TeamSide side, Vector3 position) {
  WorldPlayerState actor;
  actor.id = id;
  actor.side = side;
  actor.position = position;
  actor.active = true;
  actor.max_speed = 8.f;
  return actor;
}

bool Same(const Vector3 &a, const Vector3 &b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

void SameDecision(const PlayerControl &a, const PlayerControl &b) {
  REQUIRE(Same(a.move_direction, b.move_direction));
  REQUIRE(a.desired_speed == b.desired_speed);
  REQUIRE(a.action == b.action);
  REQUIRE(a.power == b.power);
  REQUIRE(a.look_at.has_value() == b.look_at.has_value());
  if (a.look_at) REQUIRE(Same(*a.look_at, *b.look_at));
  REQUIRE(a.target_position.has_value() == b.target_position.has_value());
  if (a.target_position) REQUIRE(Same(*a.target_position, *b.target_position));
}

std::array<TacticalBoard, 2> Shape(const WorldState &world) {
  std::array<TacticalBoard, 2> boards;
  boards[1].side = TeamSide::Away;
  for (const auto &player : world.players) {
    PlayerDirective directive;
    directive.player = player.id;
    directive.role = PlannedPlayerRole::Midfielder;
    directive.formation_position = Vector3(player.side == TeamSide::Home ? -20.f : 20.f, 0, 0);
    boards[static_cast<unsigned>(player.side)].players.push_back(directive);
  }
  return boards;
}
}  // namespace

static_assert(std::is_empty_v<football::ai::DefaultAI>);

TEST_CASE("value AI replaces outputs and omits inactive and human owned actors", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(4000000000u, TeamSide::Home, Vector3(-10, 0, 0)),
                   Actor(0, TeamSide::Away, Vector3(10, 0, 0)),
                   Actor(3, TeamSide::Home, Vector3(1, 0, 0)),
                   Actor(4, TeamSide::Home, Vector3(1, 1, 0)),
                   Actor(5, TeamSide::Away, Vector3(1, 2, 0))};
  world.players[2].active = false;
  world.players[3].externally_controlled = true;
  world.players[4].lazy = true;
  auto boards = Shape(world);
  PlayerControlSet output;
  output.Set(42, PlayerControl{});
  football::ai::DefaultAI{}.Update(world, boards, output);
  REQUIRE(output.controls().size() == 3);
  REQUIRE(output.Get(42) == nullptr);
  REQUIRE(output.Get(3) == nullptr);
  REQUIRE(output.Get(4) == nullptr);
  REQUIRE(output.Get(4000000000u) != nullptr);
  REQUIRE(output.Get(0) != nullptr);
  REQUIRE(output.Get(5)->desired_speed == 0.f);
  for (const auto &control : output.controls()) {
    REQUIRE(std::isfinite(control.desired_speed));
    REQUIRE(control.desired_speed >= 0.f);
    REQUIRE(control.desired_speed <= 8.f);
    REQUIRE(control.move_direction.coords[2] == 0.f);
    REQUIRE(control.look_at->coords[2] == 0.f);
  }
}

TEST_CASE("value AI ties follow roster order not identity", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(99, TeamSide::Home, Vector3(-5, 1, 0)),
                   Actor(1, TeamSide::Home, Vector3(-5, -1, 0))};
  auto boards = Shape(world);
  PlayerControlSet a, b;
  football::ai::DefaultAI policy;
  policy.Update(world, boards, a);
  REQUIRE(a.Get(99)->move_direction.coords[0] > 0.f);
  REQUIRE(a.Get(1)->move_direction.coords[0] < 0.f);
  world.players[0].id = 0;
  world.players[1].id = 4000000000u;
  boards = Shape(world);
  policy.Update(world, boards, b);
  SameDecision(*a.Get(99), *b.Get(0));
  SameDecision(*a.Get(1), *b.Get(4000000000u));
}

TEST_CASE("value AI replay has no hidden RNG or actor cache", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.ball_position = Vector3(5, 2, 0.11f);
  world.ball_velocity = Vector3(2, 1, 0);
  world.players = {Actor(8, TeamSide::Home, Vector3(2, 3, 0)),
                   Actor(9, TeamSide::Away, Vector3(7, 3, 0))};
  const WorldState retained = world;
  auto shape = Shape(world);
  auto planned = shape;
  football::ai::UpdateTactics(world, planned);
  PlayerControlSet expected;
  football::ai::DefaultAI{}.Update(world, planned, expected);
  for (int repeat = 0; repeat < 50; ++repeat) {
    auto fresh = shape;
    football::ai::UpdateTactics(retained, fresh);
    PlayerControlSet output;
    football::ai::DefaultAI{}.Update(retained, fresh, output);
    REQUIRE(output.controls().size() == expected.controls().size());
    for (const auto &control : output.controls()) SameDecision(control, *expected.Get(control.player));
  }
  REQUIRE(Same(world.ball_position, retained.ball_position));
  REQUIRE(Same(*shape[0].players[0].formation_position, Vector3(-20, 0, 0)));
  world.players.clear();
  REQUIRE(retained.players.size() == 2);
  PlayerControlSet empty;
  empty.Set(8, PlayerControl{});
  football::ai::DefaultAI{}.Update(world, {}, empty);
  REQUIRE(empty.controls().empty());
}

TEST_CASE("value AI chooses both attacking directions from team observations", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(10, TeamSide::Home, Vector3(40, 0, 0)),
                   Actor(20, TeamSide::Away, Vector3(-40, 0, 0))};
  for (auto &player : world.players) player.has_possession = true;
  auto boards = Shape(world);
  PlayerControlSet controls;
  football::ai::DefaultAI{}.Update(world, boards, controls);
  REQUIRE(controls.Get(10)->action == ControlAction::Shoot);
  REQUIRE(controls.Get(20)->action == ControlAction::Shoot);
  REQUIRE(controls.Get(10)->target_position->coords[0] == world.pitch.half_length());
  REQUIRE(controls.Get(20)->target_position->coords[0] == -world.pitch.half_length());
}

TEST_CASE("value AI passes to declared active recipients and releases retention", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(7, TeamSide::Home, Vector3(-50, 0, 0)),
                   Actor(8, TeamSide::Home, Vector3(-30, 0, 0)),
                   Actor(9, TeamSide::Home, Vector3(-20, 0, 0))};
  world.players[2].active = false;
  world.ball_retainer = 7;
  auto boards = Shape(world);
  boards[0].players[0].role = PlannedPlayerRole::Goalkeeper;
  PlayerControlSet controls;
  football::ai::DefaultAI{}.Update(world, boards, controls);
  REQUIRE(controls.Get(7)->action == ControlAction::ShortPass);
  REQUIRE(controls.Get(7)->target_player == 8);
  REQUIRE(controls.Get(7)->desired_speed == 0.f);
  world.players.resize(1);
  boards = Shape(world);
  boards[0].players[0].role = PlannedPlayerRole::Goalkeeper;
  football::ai::DefaultAI{}.Update(world, boards, controls);
  REQUIRE(controls.Get(7)->action == ControlAction::HighPass);
  REQUIRE_FALSE(controls.Get(7)->target_player.has_value());
}

TEST_CASE("value AI respects rule owned restart taker over tactical suggestions", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.in_set_piece = true;
  world.restart = e_GameMode_Penalty;
  world.restart_taker = 20;
  world.players = {Actor(10, TeamSide::Home, Vector3(0)),
                   Actor(20, TeamSide::Away, Vector3(-44, 0, 0))};
  world.ball_position = world.players[1].position;
  auto boards = Shape(world);
  boards[0].set_piece_taker = 10;  // A board cannot change rule authority.
  football::ai::UpdateTactics(world, boards);
  REQUIRE_FALSE(boards[0].set_piece_taker.has_value());
  REQUIRE(boards[1].set_piece_taker == 20);
  PlayerControlSet controls;
  football::ai::DefaultAI{}.Update(world, boards, controls);
  REQUIRE(controls.Get(10)->action == ControlAction::None);
  REQUIRE(controls.Get(10)->desired_speed == 0.f);
  REQUIRE(controls.Get(20)->action == ControlAction::Shoot);
  REQUIRE(controls.Get(20)->desired_speed == 0.f);
  world.in_play = false;
  football::ai::DefaultAI{}.Update(world, boards, controls);
  REQUIRE(controls.Get(20)->action == ControlAction::None);
}

TEST_CASE("tactical value policy consumes dimensions marking and run requests", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(1, TeamSide::Home, Vector3(-5, 0, 0)),
                   Actor(2, TeamSide::Away, Vector3(10, 6, 0))};
  auto boards = Shape(world);
  boards[0].width = 20.f;
  boards[0].depth = 30.f;
  boards[0].players[0].marking_target = 2;
  football::ai::UpdateTactics(world, boards);
  REQUIRE(boards[0].width == 20.f);
  REQUIRE(boards[0].depth == 30.f);
  REQUIRE(Same(*boards[0].players[0].formation_position, Vector3(5, 6, 0)));
  auto running = Shape(world);
  running[0].players[0].attacking_run = true;
  football::ai::UpdateTactics(world, running);
  REQUIRE(running[0].players[0].formation_position->coords[0] >
          Shape(world)[0].players[0].formation_position->coords[0]);
}
