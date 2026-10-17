// TACT-4: the coordinated heuristic Player policy. One joint task assignment
// per side, no ball-chasing swarm, rest defence while attacking, deterministic.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "ai/player/heuristic_policy.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/tactical_interpreter.hpp"

namespace {

using football::ai::HeuristicAssignment;
using football::ai::HeuristicPolicy;
using football::ai::TacticalBoard;
using football::math::Vector3;
using football::sim::TeamSide;

constexpr int kPlayers = 11;

football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "heuristic";
  constexpr e_PlayerRole kRoles[kPlayers] = {
      e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
      e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
      e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};
  for (int i = 0; i < kPlayers; ++i) {
    football::player::PlayerDefinition definition;
    definition.id = static_cast<football::player::PlayerId>(base_id + i);
    definition.first_name = "p";
    definition.last_name = std::to_string(i);
    definition.height = 1.8f;
    team.players.push_back(definition);
    football::sim::TacticalFormationEntry entry;
    entry.position.x = -0.85f + 0.17f * static_cast<float>(i);
    entry.position.y = (i % 2 == 0 ? -0.3f : 0.3f);
    entry.role = kRoles[i];
    team.tactical_formation.push_back(entry);
  }
  return team;
}

const football::pitch::Pitch& Pitch() {
  static const football::pitch::Pitch pitch;
  return pitch;
}

void AddTeam(WorldState& world, const football::sim::TeamSetup& team,
             TeamSide side, float sign) {
  for (int i = 0; i < kPlayers; ++i) {
    WorldPlayerState player;
    player.id = team.players[i].id;
    player.side = side;
    player.position = Vector3(sign * (2.f + 4.f * static_cast<float>(i)),
                              -18.f + 3.6f * static_cast<float>(i), 0.f);
    player.facing = Vector3(sign, 0.f, 0.f);
    player.active = true;
    player.max_speed = 8.f;
    player.role = team.tactical_formation[i].role;
    player.fatigue_inv = 1.f;
    player.time_to_ball_ms = 1000;
    player.short_pass_skill = 0.8f;
    player.workrate = 0.8f;
    world.players.push_back(player);
  }
}

WorldState MakeWorld(const football::sim::TeamSetup& home,
                     const football::sim::TeamSetup& away, bool home_has_ball) {
  WorldState world;
  world.phase = MatchPhase::FirstHalf;
  world.half_underway = true;
  world.in_play = true;
  world.ball_in_play = true;
  world.ball_position = Vector3(0.f, 0.f, 0.11f);
  world.pitch = Pitch();
  world.teams = {{{TeamSide::Home, -1, 0}, {TeamSide::Away, 1, 0}}};
  world.teams[0].has_possession = home_has_ball;
  world.teams[1].has_possession = !home_has_ball;
  world.teams[0].fading_possession_amount = 1.0f;
  world.teams[1].fading_possession_amount = 1.0f;
  AddTeam(world, home, TeamSide::Home, -1.f);
  AddTeam(world, away, TeamSide::Away, 1.f);
  if (home_has_ball) {
    world.ball_retainer = home.players[5].id;
    for (auto& player : world.players)
      if (player.id == home.players[5].id) player.has_possession = true;
  } else {
    world.ball_retainer = away.players[5].id;
    for (auto& player : world.players)
      if (player.id == away.players[5].id) player.has_possession = true;
  }
  return world;
}

std::array<TacticalBoard, 2> MakeBoards(const football::sim::TeamSetup& home,
                                        const football::sim::TeamSetup& away) {
  std::array<TacticalBoard, 2> boards;
  boards[0] = football::ai::MakeTacticalBoardV2(home, TeamSide::Home, Pitch());
  boards[1] = football::ai::MakeTacticalBoardV2(away, TeamSide::Away, Pitch());
  return boards;
}

}  // namespace

TEST_CASE("the heuristic emits a complete legal control set",
          "[ai][tactical][heuristic]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = MakeBoards(home, away);
  WorldState world = MakeWorld(home, away, true);
  HeuristicPolicy policy;
  PlayerControlSet output;
  policy.Update(world, boards, output);

  CHECK(output.controls().size() == 2 * kPlayers);
  for (const auto& control : output.controls()) {
    CHECK(std::isfinite(control.move_direction.coords[0]));
    CHECK(std::isfinite(control.move_direction.coords[1]));
    CHECK(std::isfinite(control.desired_speed));
    CHECK(control.desired_speed >= 0.0f);
    CHECK(control.desired_speed <= 8.0f + 1e-4f);
  }
}

TEST_CASE("defending assigns exactly one presser and one cover",
          "[ai][tactical][heuristic]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = MakeBoards(home, away);
  WorldState world = MakeWorld(home, away, false);  // home defends
  HeuristicPolicy policy;
  PlayerControlSet output;
  policy.Update(world, boards, output);

  int pressers = 0;
  int covers = 0;
  int holders = 0;
  for (const HeuristicAssignment& assignment : policy.LastAssignment(TeamSide::Home)) {
    if (assignment.presser) ++pressers;
    if (assignment.cover) ++covers;
    if (assignment.holder) ++holders;
  }
  CHECK(pressers == 1);
  CHECK(covers == 1);
  CHECK(holders == 0);

  int away_holders = 0;
  for (const HeuristicAssignment& assignment : policy.LastAssignment(TeamSide::Away))
    if (assignment.holder) ++away_holders;
  CHECK(away_holders == 1);

  // No swarm: the defending targets do not collapse onto one point.
  std::vector<Vector3> targets;
  for (const HeuristicAssignment& assignment : policy.LastAssignment(TeamSide::Home))
    targets.push_back(assignment.target);
  int near_duplicates = 0;
  for (std::size_t i = 0; i < targets.size(); ++i)
    for (std::size_t j = i + 1; j < targets.size(); ++j)
      if ((targets[i] - targets[j]).GetLength() < 1.0f) ++near_duplicates;
  CHECK(near_duplicates <= 2);
}

TEST_CASE("attacking keeps a two-man rest defence behind the ball",
          "[ai][tactical][heuristic]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = MakeBoards(home, away);
  WorldState world = MakeWorld(home, away, true);
  HeuristicPolicy policy;
  PlayerControlSet output;
  policy.Update(world, boards, output);

  const float attack = 1.0f;  // home attacks +x
  int rest = 0;
  for (const HeuristicAssignment& assignment : policy.LastAssignment(TeamSide::Home)) {
    if (!assignment.rest_defence) continue;
    ++rest;
    CHECK(assignment.target.coords[0] * attack <=
          world.ball_position.coords[0] * attack - 3.5f);
  }
  CHECK(rest == 2);
}

TEST_CASE("the heuristic is deterministic and reset clears memory",
          "[ai][tactical][heuristic]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = MakeBoards(home, away);
  WorldState world = MakeWorld(home, away, false);

  HeuristicPolicy first;
  PlayerControlSet first_output;
  first.Update(world, boards, first_output);

  HeuristicPolicy second;
  PlayerControlSet second_output;
  second.Update(world, boards, second_output);

  REQUIRE(first_output.controls().size() == second_output.controls().size());
  for (std::size_t i = 0; i < first_output.controls().size(); ++i) {
    const auto& a = first_output.controls()[i];
    const auto* b = second_output.Get(a.player);
    REQUIRE(b != nullptr);
    CHECK(a.move_direction.coords[0] == b->move_direction.coords[0]);
    CHECK(a.move_direction.coords[1] == b->move_direction.coords[1]);
    CHECK(a.desired_speed == b->desired_speed);
    CHECK(a.action == b->action);
  }

  first.Reset();
  CHECK(first.LastAssignment(TeamSide::Home).empty());
  CHECK(first.PossessionTicks(home.players[0].id) == 0);
}

TEST_CASE("a high defensive line pushes the block up the pitch",
          "[ai][tactical][heuristic]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away, false);  // home defends

  auto run = [&](float line) {
    auto boards = MakeBoards(home, away);
    boards[0].advanced->out_of_possession.defensive_line = line;
    HeuristicPolicy policy;
    PlayerControlSet output;
    policy.Update(world, boards, output);
    float sum = 0.0f;
    int count = 0;
    for (const HeuristicAssignment& assignment :
         policy.LastAssignment(TeamSide::Home)) {
      if (assignment.presser || assignment.cover) continue;
      sum += assignment.target.coords[0];
      ++count;
    }
    return sum / static_cast<float>(std::max(count, 1));
  };

  CHECK(run(0.85f) > run(0.2f));
}
