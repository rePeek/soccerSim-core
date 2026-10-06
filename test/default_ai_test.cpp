#include <array>
#include <cmath>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "ai/default_ai.hpp"

using football::ai::PlayerDirective;
using football::ai::PlannedPlayerRole;

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

TEST_CASE("value AI replaces outputs and omits only inactive actors", "[ai]") {
  WorldState world;
  world.in_play = true;
  world.players = {Actor(4000000000u, TeamSide::Home, Vector3(-10, 0, 0)),
                   Actor(0, TeamSide::Away, Vector3(10, 0, 0)),
                   Actor(3, TeamSide::Home, Vector3(1, 0, 0)),
                   Actor(4, TeamSide::Home, Vector3(1, 1, 0)),
                   Actor(5, TeamSide::Away, Vector3(1, 2, 0))};
  world.players[2].active = false;
  world.players[4].lazy = true;
  auto policy = Shape(world);
  PlayerControlSet output;
  output.Set(42, PlayerControl{});
  policy.Update(world, output);
  REQUIRE(output.controls().size() == 4);
  REQUIRE(output.Get(42) == nullptr);
  REQUIRE(output.Get(3) == nullptr);
  REQUIRE(output.Get(4) != nullptr);
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
  board.width = 0.8f;
  board.depth = 0.6f;
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
  REQUIRE(board.width == 0.8f);
  REQUIRE(board.depth == 0.6f);
  REQUIRE(board.players[0].marking_target == 9);
  REQUIRE(policy.tactics(TeamSide::Away).width == 0.75f);
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
  world.simulation_epoch = ObservationEpoch::New();
  world.players = {Actor(1, TeamSide::Home, Vector3(-1, 0, 0)),
                   Actor(2, TeamSide::Home, Vector3(-10, 0, 0)),
                   Actor(3, TeamSide::Away, Vector3(10, 6, 0))};
  auto policy = Shape(world);
  auto &board = policy.tactics(TeamSide::Home);
  board.players[1].formation_position = Vector3(-20, 10, 0);
  PlayerControlSet before, after;
  policy.Update(world, before);
  board.width = 20.f / world.pitch.width();
  board.depth = 30.f / world.pitch.length();
  policy.Update(world, after);
  REQUIRE_FALSE(Same(before.Get(2)->move_direction, after.Get(2)->move_direction));
  board.players[1].marking_target = 3;
  policy.Update(world, after);
  REQUIRE(after.Get(2)->move_direction.coords[0] > 0.f);
  REQUIRE(after.Get(2)->move_direction.coords[1] > 0.f);
  REQUIRE(Same(*board.players[1].formation_position, Vector3(-20, 10, 0)));
  board.players[1].marking_target.reset();
  REQUIRE(policy.RequestAttackingRun(TeamSide::Home, world, 2));
  policy.Update(world, after);
  REQUIRE(after.Get(2)->move_direction.coords[0] > 0.f);
  policy.ResetRequests();
  policy.Update(world, before);
  REQUIRE(before.Get(2)->move_direction.coords[0] < 0.f);
  REQUIRE(policy.RequestTeamPressure(TeamSide::Home, world, 1));
  policy.Update(world, after);
  REQUIRE(after.Get(2)->move_direction.coords[0] > 0.f);
}

TEST_CASE("AI instances copies and side boards own independent tactical values", "[ai]") {
  DefaultAI original;
  original.tactics(TeamSide::Home).width = 0.9f;
  original.tactics(TeamSide::Home).players.push_back(PlayerDirective{});
  auto copy = original;
  copy.tactics(TeamSide::Home).players[0].role = PlannedPlayerRole::Forward;
  copy.tactics(TeamSide::Home).width = 0.6f;
  REQUIRE(original.tactics(TeamSide::Home).width == 0.9f);
  REQUIRE(original.tactics(TeamSide::Home).players[0].role == PlannedPlayerRole::Unspecified);
  REQUIRE(original.tactics(TeamSide::Away).side == TeamSide::Away);
  REQUIRE(original.tactics(TeamSide::Away).players.empty());
  REQUIRE(DefaultAI{}.tactics(TeamSide::Home).players.empty());
}

TEST_CASE("AI bootstraps independent tactical boards directly from static models", "[ai]") {
  namespace model = football::model;
  model::Team home, away;
  home.players.resize(3);
  home.players[0].id = 4000000000u;
  home.players[1].id = 17;
  home.players[2].id = 42;
  home.tactical_formation = {{{-1.f, 0.f}, e_PlayerRole_GK},
                            {{-0.8f, 0.8f}, e_PlayerRole_LB},
                            {{1.f, 0.f}, e_PlayerRole_CF}};
  away = home;
  for (auto &player : away.players) player.id -= 1;
  DefaultAI policy(home, away);
  const auto &board = policy.tactics(TeamSide::Home);
  const auto &opponent = policy.tactics(TeamSide::Away);
  REQUIRE(board.side == TeamSide::Home);
  REQUIRE(opponent.side == TeamSide::Away);
  REQUIRE(board.width == 0.75f);
  REQUIRE(board.depth == 0.55f);
  REQUIRE(board.players.size() == 3);
  REQUIRE(board.players[0].player == 4000000000u);
  REQUIRE(board.players[1].player == 17);
  REQUIRE(board.players[0].role == PlannedPlayerRole::Goalkeeper);
  REQUIRE(board.players[1].role == PlannedPlayerRole::Defender);
  REQUIRE(board.players[2].role == PlannedPlayerRole::Forward);
  REQUIRE(board.players[0].formation_position->coords[0] < 0.f);
  for (std::size_t i = 0; i < board.players.size(); ++i) {
    REQUIRE(board.players[i].formation_position->coords[0] ==
            -opponent.players[i].formation_position->coords[0]);
    REQUIRE(board.players[i].formation_position->coords[1] ==
            -opponent.players[i].formation_position->coords[1]);
    REQUIRE_FALSE(board.players[i].marking_target.has_value());
  }
  const auto anchor = board.players[1].formation_position;
  home.players[1].id = 99;
  home.tactical_formation[1].position = {1.f, 0.f};
  REQUIRE(board.players[1].player == 17);
  REQUIRE(board.players[1].formation_position == anchor);
  REQUIRE(DefaultAI(model::Team{}, model::Team{}).tactics(TeamSide::Home).players.empty());
}

TEST_CASE("AI bootstrap respects formation precedence and never invents roster profiles", "[ai]") {
  namespace model = football::model;
  model::Team team;
  team.players.resize(3);
  for (unsigned i = 0; i < 3; ++i) team.players[i].id = i + 1;
  team.tactical_formation = {{{1.f, 0.f}, e_PlayerRole_CF},
                            {{-1.f, 0.f}, e_PlayerRole_GK}};
  team.formation = {{{-0.2f, 0.1f}, e_PlayerRole_CM}};
  auto board = football::ai::MakeTacticalBoard(team, TeamSide::Home, model::MakeLegacyPitch());
  REQUIRE(board.players.size() == 1);
  REQUIRE(board.players[0].role == PlannedPlayerRole::Midfielder);
  REQUIRE(board.players[0].formation_position->coords[0] > 0.f);
  team.tactical_formation.clear();
  board = football::ai::MakeTacticalBoard(team, TeamSide::Home, model::MakeLegacyPitch());
  REQUIRE(board.players[0].formation_position->coords[0] < 0.f);
  REQUIRE(board.players[0].formation_position->coords[1] < 0.f);
  team.formation.clear();
  board = football::ai::MakeTacticalBoard(team, TeamSide::Home, model::MakeLegacyPitch());
  REQUIRE(board.players.size() == team.players.size());
  team.tactical_formation.resize(5); // Invalid sim input may still be inspected before startup.
  board = football::ai::MakeTacticalBoard(team, TeamSide::Home, model::MakeLegacyPitch());
  REQUIRE(board.players.size() == 3);
  REQUIRE(board.players[2].player == 3);
}

TEST_CASE("AI initial desired shape deterministically spaces coincident outfield anchors", "[ai]") {
  namespace model = football::model;
  model::Team team;
  team.players.resize(2);
  team.players[0].id = 1;
  team.players[1].id = 2;
  team.tactical_formation = {{{0.f, 0.f}, e_PlayerRole_CF}, {{0.f, 0.f}, e_PlayerRole_CF}};
  const auto pitch = model::MakeLegacyPitch();
  auto first = football::ai::MakeTacticalBoard(team, TeamSide::Home, pitch);
  auto second = football::ai::MakeTacticalBoard(team, TeamSide::Home, pitch);
  REQUIRE(first.players[0].formation_position->coords[1] > 0.f);
  REQUIRE(first.players[1].formation_position->coords[1] < 0.f);
  for (unsigned i = 0; i < 2; ++i)
    REQUIRE(Same(*first.players[i].formation_position, *second.players[i].formation_position));
}

TEST_CASE("transient team requests use observed ticks without changing tactical plans", "[ai][requests]") {
  WorldState world;
  world.in_play = true; world.tick = 100;
  world.simulation_epoch = ObservationEpoch::New();
  world.players = {Actor(9, TeamSide::Home, Vector3(-50, 0, 0)),
                   Actor(8, TeamSide::Home, Vector3(-1, 0, 0)),
                   Actor(7, TeamSide::Home, Vector3(-10, 10, 0)),
                   Actor(100, TeamSide::Away, Vector3(10, 0, 0))};
  auto policy = Shape(world);
  policy.tactics(TeamSide::Home).players[0].role = PlannedPlayerRole::Goalkeeper;
  policy.tactics(TeamSide::Home).width = 0.9f;
  const auto board = policy.tactics(TeamSide::Home);
  REQUIRE(policy.RequestAttackingRun(TeamSide::Home, world, 7));
  REQUIRE(policy.RequestTeamPressure(TeamSide::Home, world, 8));
  REQUIRE(policy.RequestKeeperRush(TeamSide::Home, world));
  REQUIRE(policy.requests(TeamSide::Home).attacking_run.player == 7);
  REQUIRE(policy.requests(TeamSide::Home).pressure.marking_target == 100);
  REQUIRE(policy.requests(TeamSide::Home).keeper_rush.player == 9);
  REQUIRE_FALSE(policy.requests(TeamSide::Away).pressure.player.has_value());
  PlayerControlSet baseline, requested;
  auto quiet = policy; quiet.ResetRequests();
  quiet.Update(world, baseline); policy.Update(world, requested);
  REQUIRE_FALSE(Same(baseline.Get(7)->move_direction, requested.Get(7)->move_direction));
  REQUIRE_FALSE(Same(baseline.Get(9)->move_direction, requested.Get(9)->move_direction));
  const auto retained_requests = policy.requests(TeamSide::Home);
  REQUIRE(retained_requests.attacking_run.Active(499, world.reset_sequence, world.simulation_epoch));
  REQUIRE_FALSE(retained_requests.attacking_run.Active(500, world.reset_sequence, world.simulation_epoch));
  REQUIRE(retained_requests.pressure.Active(149, world.reset_sequence, world.simulation_epoch));
  REQUIRE_FALSE(retained_requests.pressure.Active(150, world.reset_sequence, world.simulation_epoch));
  REQUIRE(retained_requests.keeper_rush.Active(129, world.reset_sequence, world.simulation_epoch));
  REQUIRE_FALSE(retained_requests.keeper_rush.Active(130, world.reset_sequence, world.simulation_epoch));
  REQUIRE_FALSE(retained_requests.attacking_run.Active(99, world.reset_sequence, world.simulation_epoch));
  world.tick = 500;
  policy.Update(world, requested); quiet.Update(world, baseline);
  for (const auto &control : requested.controls()) SameDecision(control, *baseline.Get(control.player));
  REQUIRE(policy.tactics(TeamSide::Home).width == board.width);
  REQUIRE(Same(*policy.tactics(TeamSide::Home).players[2].formation_position,
               *board.players[2].formation_position));
  policy.ResetRequests();
  REQUIRE_FALSE(policy.requests(TeamSide::Home).attacking_run.player.has_value());
  REQUIRE(retained_requests.attacking_run.player == 7);
}

TEST_CASE("team requests reject wrong sides missing profiles and suppress inactive targets", "[ai][requests]") {
  WorldState world; world.in_play = true; world.tick = 50;
  world.simulation_epoch = ObservationEpoch::New();
  world.players = {Actor(9, TeamSide::Home, Vector3(-50, 0, 0)),
                   Actor(8, TeamSide::Home, Vector3(-1, 0, 0)),
                   Actor(7, TeamSide::Home, Vector3(-10, 10, 0)),
                   Actor(100, TeamSide::Away, Vector3(10, 0, 0))};
  auto policy = Shape(world);
  policy.tactics(TeamSide::Home).players[0].role = PlannedPlayerRole::Goalkeeper;
  REQUIRE_FALSE(policy.RequestAttackingRun(TeamSide::Home, world, 100));
  REQUIRE_FALSE(policy.RequestAttackingRun(TeamSide::Home, world, 123));
  REQUIRE(policy.RequestAttackingRun(TeamSide::Home, world, 7));
  REQUIRE_FALSE(policy.RequestAttackingRun(TeamSide::Home, world, 9));
  REQUIRE_FALSE(policy.requests(TeamSide::Home).attacking_run.player.has_value());
  world.players[2].lazy = true;
  REQUIRE_FALSE(policy.RequestAttackingRun(TeamSide::Home, world, 7));
  world.players[2].lazy = false;
  REQUIRE(policy.RequestAttackingRun(TeamSide::Home, world, 7));
  REQUIRE(policy.RequestTeamPressure(TeamSide::Home, world, 8));
  auto quiet = policy; quiet.ResetRequests();
  world.players[2].active = false;
  PlayerControlSet requested, baseline;
  policy.Update(world, requested); quiet.Update(world, baseline);
  REQUIRE(requested.Get(7) == nullptr);
  for (const auto &control : requested.controls()) SameDecision(control, *baseline.Get(control.player));
  world.players[2].active = true;
  world.ball_retainer = 7;
  REQUIRE(policy.RequestAttackingRun(TeamSide::Home, world));
  REQUIRE(policy.requests(TeamSide::Home).attacking_run.player == 8);
  world.players.push_back(Actor(101, TeamSide::Away, Vector3(40, 0, 0)));
  world.ball_retainer = 101;
  REQUIRE(policy.RequestTeamPressure(TeamSide::Home, world));
  REQUIRE(policy.requests(TeamSide::Home).pressure.marking_target == 101);
  world.players.pop_back();
  world.ball_retainer.reset();
  world.players[2].active = true; world.players[3].active = false;
  policy.ResetRequests();
  REQUIRE_FALSE(policy.RequestTeamPressure(TeamSide::Home, world));
  world.in_set_piece = true;
  REQUIRE_FALSE(policy.RequestAttackingRun(TeamSide::Home, world));
  REQUIRE_FALSE(policy.RequestKeeperRush(TeamSide::Home, world));
}

TEST_CASE("request replacement copying replay and stopped play have explicit lifetimes", "[ai][requests]") {
  WorldState world; world.in_play = true; world.tick = 20;
  world.simulation_epoch = ObservationEpoch::New();
  world.players = {Actor(1, TeamSide::Home, Vector3(-1, 0, 0)),
                   Actor(2, TeamSide::Home, Vector3(-10, 10, 0))};
  auto policy = Shape(world);
  REQUIRE(policy.RequestAttackingRun(TeamSide::Home, world, 2));
  auto copy = policy;
  world.tick = 30;
  REQUIRE(copy.RequestAttackingRun(TeamSide::Home, world, 1));
  REQUIRE(policy.requests(TeamSide::Home).attacking_run.player == 2);
  REQUIRE(copy.requests(TeamSide::Home).attacking_run.player == 1);
  REQUIRE(policy.requests(TeamSide::Home).attacking_run.issued_tick == 20);
  PlayerControlSet a, b;
  const auto snapshot = world;
  policy.Update(snapshot, a); policy.Update(snapshot, b);
  for (const auto &control : a.controls()) SameDecision(control, *b.Get(control.player));
  world.in_play = false; policy.Update(world, a);
  for (const auto &control : a.controls()) REQUIRE(control.desired_speed == 0.f);
  world.in_play = true;
  ++world.reset_sequence;  // A rule reset invalidates requests even without elapsed time.
  auto no_requests = policy; no_requests.ResetRequests();
  no_requests.Update(world, b); policy.Update(world, a);
  for (const auto &control : a.controls()) SameDecision(control, *b.Get(control.player));
  policy.ResetRequests(); world.tick = 0; world.in_play = true;
  auto fresh = Shape(world); fresh.Update(world, b); policy.Update(world, a);
  for (const auto &control : a.controls()) SameDecision(control, *b.Get(control.player));
}

TEST_CASE("request validity uses owning epoch values not clock rollback heuristics", "[ai][requests]") {
  WorldState world; world.in_play = true; world.tick = 100;
  world.players = {Actor(9, TeamSide::Home, Vector3(-50, 0, 0)),
                   Actor(8, TeamSide::Home, Vector3(-1, 0, 0)),
                   Actor(7, TeamSide::Home, Vector3(-10, 10, 0)),
                   Actor(100, TeamSide::Away, Vector3(10, 0, 0))};
  auto policy = Shape(world);
  policy.tactics(TeamSide::Home).players[0].role = PlannedPlayerRole::Goalkeeper;
  // Unbound synthetic snapshots cannot issue durable transient requests.
  REQUIRE_FALSE(policy.RequestAttackingRun(TeamSide::Home, world, 7));
  REQUIRE_FALSE(policy.RequestTeamPressure(TeamSide::Home, world));
  REQUIRE_FALSE(policy.RequestKeeperRush(TeamSide::Home, world));
  world.simulation_epoch = ObservationEpoch::New();
  REQUIRE(policy.RequestAttackingRun(TeamSide::Home, world, 7));
  REQUIRE(policy.RequestTeamPressure(TeamSide::Home, world, 8));
  REQUIRE(policy.RequestKeeperRush(TeamSide::Home, world));
  const auto old_world = world;
  const auto retained = policy.requests(TeamSide::Home);
  auto copy = policy;
  world.simulation_epoch = ObservationEpoch::New(); // Same tick/reset count, different Match.
  auto quiet = policy; quiet.ResetRequests();
  PlayerControlSet a, b;
  for (std::uint64_t tick = 100; tick <= 501; ++tick) {
    world.tick = tick;
    REQUIRE_FALSE(retained.attacking_run.Active(tick, world.reset_sequence, world.simulation_epoch));
    REQUIRE_FALSE(retained.pressure.Active(tick, world.reset_sequence, world.simulation_epoch));
    REQUIRE_FALSE(retained.keeper_rush.Active(tick, world.reset_sequence, world.simulation_epoch));
    quiet.Update(world, b); copy.Update(world, a);
    for (const auto &control : a.controls()) SameDecision(control, *b.Get(control.player));
  }
  // Retained old snapshots/policies remain usable for deliberate replay.
  REQUIRE(retained.attacking_run.Active(old_world.tick, old_world.reset_sequence, old_world.simulation_epoch));
  policy.Update(old_world, a); copy.Update(old_world, b);
  for (const auto &control : a.controls()) SameDecision(control, *b.Get(control.player));
  REQUIRE(policy.requests(TeamSide::Home).attacking_run.simulation_epoch == old_world.simulation_epoch);
}
