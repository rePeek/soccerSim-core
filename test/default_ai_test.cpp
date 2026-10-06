#include <array>
#include <cmath>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "ai/default_ai.hpp"

namespace {
using blunted::Vector3;
using football::model::TeamSide;
using football::ai::DefaultAI;

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
  REQUIRE(a.target_player == b.target_player);
  REQUIRE(a.look_at.has_value() == b.look_at.has_value());
  if (a.look_at) REQUIRE(Same(*a.look_at, *b.look_at));
  REQUIRE(a.target_position.has_value() == b.target_position.has_value());
  if (a.target_position) REQUIRE(Same(*a.target_position, *b.target_position));
}

DefaultAI Shape(const WorldState &world) {
  DefaultAI policy;
  for (const auto &player : world.players) {
    PlayerDirective directive;
    directive.player = player.id;
    directive.role = PlannedPlayerRole::Midfielder;
    directive.formation_position = Vector3(player.side == TeamSide::Home ? -20.f : 20.f, 0, 0);
    policy.tactics(player.side).players.push_back(directive);
  }
  return policy;
}
}  // namespace

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
  auto policy = Shape(world);
  PlayerControlSet output;
  output.Set(42, PlayerControl{});
  policy.Update(world, output);
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
  auto policy = Shape(world);
  PlayerControlSet a, b;
  policy.Update(world, a);
  REQUIRE(a.Get(99)->move_direction.coords[0] > 0.f);
  REQUIRE(a.Get(1)->move_direction.coords[0] < 0.f);
  world.players[0].id = 0;
  world.players[1].id = 4000000000u;
  policy = Shape(world);
  policy.Update(world, b);
  SameDecision(*a.Get(99), *b.Get(0));
  SameDecision(*a.Get(1), *b.Get(4000000000u));
}

TEST_CASE("persistent tactical intent does not accumulate positioning or hidden state", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.ball_position = Vector3(5, 2, 0.11f);
  world.ball_velocity = Vector3(2, 1, 0);
  world.players = {Actor(8, TeamSide::Home, Vector3(2, 3, 0)),
                   Actor(9, TeamSide::Away, Vector3(7, 3, 0))};
  const WorldState retained = world;
  auto policy = Shape(world);
  auto &board = policy.tactics(TeamSide::Home);
  board.width = 20.f;
  board.depth = 30.f;
  board.players[0].marking_target = 9;
  PlayerControlSet expected;
  policy.Update(world, expected);
  for (int repeat = 0; repeat < 50; ++repeat) {
    PlayerControlSet output;
    policy.Update(retained, output);
    REQUIRE(output.controls().size() == expected.controls().size());
    for (const auto &control : output.controls()) SameDecision(control, *expected.Get(control.player));
  }
  REQUIRE(Same(world.ball_position, retained.ball_position));
  REQUIRE(Same(*board.players[0].formation_position, Vector3(-20, 0, 0)));
  REQUIRE(board.width == 20.f);
  REQUIRE(board.depth == 30.f);
  REQUIRE(board.players[0].marking_target == 9);
  REQUIRE(policy.tactics(TeamSide::Away).width == 0.f);
  world.players.clear();
  REQUIRE(retained.players.size() == 2);
  policy.Update(world, expected);
  REQUIRE(expected.controls().empty());
  REQUIRE(board.players.size() == 1);
}

TEST_CASE("value AI chooses both attacking directions from team observations", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(10, TeamSide::Home, Vector3(40, 0, 0)),
                   Actor(20, TeamSide::Away, Vector3(-40, 0, 0))};
  for (auto &player : world.players) player.has_possession = true;
  auto policy = Shape(world);
  PlayerControlSet controls;
  policy.Update(world, controls);
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
  auto policy = Shape(world);
  policy.tactics(TeamSide::Home).players[0].role = PlannedPlayerRole::Goalkeeper;
  PlayerControlSet controls;
  policy.Update(world, controls);
  REQUIRE(controls.Get(7)->action == ControlAction::ShortPass);
  REQUIRE(controls.Get(7)->target_player == 8);
  REQUIRE(controls.Get(7)->desired_speed == 0.f);
  world.players.resize(1);
  policy.Update(world, controls);
  REQUIRE(controls.Get(7)->action == ControlAction::HighPass);
  REQUIRE_FALSE(controls.Get(7)->target_player.has_value());
}

TEST_CASE("value AI reads restart authority only from WorldState", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.in_set_piece = true;
  world.restart = e_GameMode_Penalty;
  world.restart_taker = 20;
  world.players = {Actor(10, TeamSide::Home, Vector3(0)),
                   Actor(20, TeamSide::Away, Vector3(-44, 0, 0))};
  world.ball_position = world.players[1].position;
  auto policy = Shape(world);
  PlayerControlSet controls;
  policy.Update(world, controls);
  REQUIRE(controls.Get(10)->action == ControlAction::None);
  REQUIRE(controls.Get(10)->desired_speed == 0.f);
  REQUIRE(controls.Get(20)->action == ControlAction::Shoot);
  REQUIRE(controls.Get(20)->desired_speed == 0.f);
  world.in_play = false;
  policy.Update(world, controls);
  REQUIRE(controls.Get(20)->action == ControlAction::None);
}

TEST_CASE("external tactical edits persist and change decisions without rewriting anchors", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(1, TeamSide::Home, Vector3(-1, 0, 0)),
                   Actor(2, TeamSide::Home, Vector3(-10, 0, 0)),
                   Actor(3, TeamSide::Away, Vector3(10, 6, 0))};
  auto policy = Shape(world);
  auto &board = policy.tactics(TeamSide::Home);
  board.players[1].formation_position = Vector3(-20, 10, 0);
  PlayerControlSet before, after;
  policy.Update(world, before);
  board.width = 20.f;
  board.depth = 30.f;
  policy.Update(world, after);
  REQUIRE_FALSE(Same(before.Get(2)->move_direction, after.Get(2)->move_direction));
  board.players[1].marking_target = 3;
  policy.Update(world, after);
  REQUIRE(after.Get(2)->move_direction.coords[0] > 0.f);
  REQUIRE(after.Get(2)->move_direction.coords[1] > 0.f);
  REQUIRE(Same(*board.players[1].formation_position, Vector3(-20, 10, 0)));
  board.players[1].marking_target.reset();
  world.players[1].attacking_run_remaining_ms = 1800;
  policy.Update(world, after);
  REQUIRE(after.Get(2)->move_direction.coords[0] > 0.f);
  world.players[1].attacking_run_remaining_ms = 0;
  policy.Update(world, before);
  REQUIRE(before.Get(2)->move_direction.coords[0] < 0.f);
  world.players[1].pressure_remaining_ms = 300;
  policy.Update(world, after);
  REQUIRE(after.Get(2)->move_direction.coords[0] > 0.f);
}

TEST_CASE("AI instances copies and side boards own independent tactical values", "[ai]") {
  DefaultAI original;
  original.tactics(TeamSide::Home).width = 42.f;
  original.tactics(TeamSide::Home).players.push_back(PlayerDirective{});
  auto copy = original;
  copy.tactics(TeamSide::Home).players[0].role = PlannedPlayerRole::Forward;
  copy.tactics(TeamSide::Home).width = 20.f;
  REQUIRE(original.tactics(TeamSide::Home).width == 42.f);
  REQUIRE(original.tactics(TeamSide::Home).players[0].role == PlannedPlayerRole::Unspecified);
  REQUIRE(original.tactics(TeamSide::Away).side == TeamSide::Away);
  REQUIRE(original.tactics(TeamSide::Away).players.empty());
  REQUIRE(DefaultAI{}.tactics(TeamSide::Home).players.empty());
}
