// TACT-2: pure TacticalInterpreter semantics, dual-phase shape and team scalars.
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <optional>
#include <string>

#include "football/ai/tactical_interpreter.hpp"

namespace {

using Catch::Approx;
using football::ai::EffectivePlayerTactics;
using football::ai::InterpretationState;
using football::ai::MarkingStyle;
using football::ai::SlotAssignment;
using football::ai::TacticalBoard;
using football::ai::TacticalInterpretation;
using football::ai::TacticalInterpreter;
using football::ai::TacticalPhase;
using football::ai::TacticalPlanV2;
using football::sim::TeamSide;

constexpr int kSlots = 11;

football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "interp";
  constexpr e_PlayerRole kRoles[kSlots] = {
      e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
      e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
      e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};
  for (int i = 0; i < kSlots; ++i) {
    football::player::PlayerDefinition definition;
    definition.id = static_cast<football::player::PlayerId>(base_id + i);
    definition.first_name = "p";
    definition.last_name = std::to_string(i);
    definition.height = 1.8f;
    team.players.push_back(definition);
    football::team::TacticalFormationEntry entry;
    entry.position.x = -0.85f + 0.17f * static_cast<float>(i);
    entry.position.y = (i % 2 == 0 ? -0.3f : 0.3f);
    entry.role = kRoles[i];
    team.tactical_formation.push_back(entry);
  }
  return team;
}

std::array<SlotAssignment, 2> Assign(const football::sim::TeamSetup& home,
                                     const football::sim::TeamSetup& away) {
  std::array<SlotAssignment, 2> result;
  for (int i = 0; i < kSlots; ++i) {
    result[0].players[i] = home.players[i].id;
    result[1].players[i] = away.players[i].id;
  }
  return result;
}

WorldState MakeWorld() {
  WorldState world;
  world.phase = MatchPhase::FirstHalf;
  world.half_underway = true;
  world.in_play = true;
  world.ball_in_play = true;
  world.ball_position = football::math::Vector3(0.0f, 0.0f, 0.11f);
  world.pitch = football::pitch::Pitch{};
  world.teams = {{{TeamSide::Home, -1, 0}, {TeamSide::Away, 1, 0}}};
  return world;
}

const football::pitch::Pitch& world_pitch();

std::array<TacticalBoard, 2> Boards(const football::sim::TeamSetup& home,
                                    const football::sim::TeamSetup& away) {
  std::array<TacticalBoard, 2> boards;
  boards[0] = football::ai::MakeTacticalBoardV2(home, TeamSide::Home,
                                                world_pitch());
  boards[1] = football::ai::MakeTacticalBoardV2(away, TeamSide::Away,
                                                world_pitch());
  return boards;
}

const football::pitch::Pitch& world_pitch() {
  static const football::pitch::Pitch pitch;
  return pitch;
}

TacticalInterpretation Interpret(std::array<TacticalBoard, 2>& boards,
                                 const std::array<SlotAssignment, 2>& assignments,
                                 WorldState& world,
                                 const InterpretationState& state) {
  return TacticalInterpreter::Build(boards, world, assignments, state);
}

TacticalPlanV2& Plan(std::array<TacticalBoard, 2>& boards, TeamSide side) {
  return *football::ai::AdvancedPlan(boards[static_cast<unsigned>(side)]);
}

}  // namespace

TEST_CASE("default plan interprets to finite mirrored anchors",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = Boards(home, away);
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();
  InterpretationState state;
  state.phase = {TacticalPhase::InPossession, TacticalPhase::InPossession};

  const auto result = Interpret(boards, assignments, world, state);
  for (std::size_t side = 0; side < 2; ++side) {
    CHECK(result.teams[side].has_plan);
    for (const auto& player : result.players[side]) {
      CHECK(player.assigned);
      CHECK(std::isfinite(player.target_anchor_metres.coords[0]));
      CHECK(std::isfinite(player.target_anchor_metres.coords[1]));
      CHECK(player.target_anchor_metres.coords[0] >= -world.pitch.half_length());
      CHECK(player.target_anchor_metres.coords[0] <= world.pitch.half_length());
    }
  }
  // Identical plans and assignments -> the two sides are point mirrors.
  for (std::size_t i = 0; i < result.players[0].size(); ++i) {
    CHECK(result.players[0][i].target_anchor_metres.coords[0] ==
          Approx(-result.players[1][i].target_anchor_metres.coords[0]));
    CHECK(result.players[0][i].target_anchor_metres.coords[1] ==
          Approx(-result.players[1][i].target_anchor_metres.coords[1]));
  }
}

TEST_CASE("in-possession and out-of-possession anchors are independent",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = Boards(home, away);
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();

  auto& plan = Plan(boards, TeamSide::Home);
  constexpr std::size_t kStriker = 9;
  plan.slots[kStriker].in_possession = {0.9f, 0.0f};
  plan.slots[kStriker].out_of_possession = {0.2f, 0.0f};

  InterpretationState attacking;
  attacking.phase = {TacticalPhase::InPossession, TacticalPhase::OutOfPossession};
  const auto forward = Interpret(boards, assignments, world, attacking);
  InterpretationState defending;
  defending.phase = {TacticalPhase::OutOfPossession, TacticalPhase::OutOfPossession};
  const auto deep = Interpret(boards, assignments, world, defending);

  CHECK(forward.players[0][kStriker].target_anchor_metres.coords[0] >
        deep.players[0][kStriker].target_anchor_metres.coords[0]);
}

TEST_CASE("defensive line and pressing intensity move targets monotonically",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();
  InterpretationState state;
  state.phase = {TacticalPhase::OutOfPossession, TacticalPhase::OutOfPossession};

  constexpr std::size_t kCentreBack = 2;
  auto boards_low = Boards(home, away);
  auto boards_high = Boards(home, away);
  Plan(boards_low, TeamSide::Home).out_of_possession.defensive_line = 0.2f;
  Plan(boards_high, TeamSide::Home).out_of_possession.defensive_line = 0.8f;
  const auto low = Interpret(boards_low, assignments, world, state);
  const auto high = Interpret(boards_high, assignments, world, state);
  CHECK(high.players[0][kCentreBack].target_anchor_metres.coords[0] >
        low.players[0][kCentreBack].target_anchor_metres.coords[0]);

  constexpr std::size_t kStriker = 9;
  auto boards_weak = Boards(home, away);
  auto boards_strong = Boards(home, away);
  Plan(boards_weak, TeamSide::Home).out_of_possession.pressing_intensity = 0.1f;
  Plan(boards_strong, TeamSide::Home).out_of_possession.pressing_intensity = 0.9f;
  const auto weak = Interpret(boards_weak, assignments, world, state);
  const auto strong = Interpret(boards_strong, assignments, world, state);
  CHECK(strong.players[0][kStriker].press_bias >
        weak.players[0][kStriker].press_bias);
  CHECK(strong.teams[0].press > weak.teams[0].press);
}

TEST_CASE("width spreads and compactness narrows the block",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();
  InterpretationState attacking;
  attacking.phase = {TacticalPhase::InPossession, TacticalPhase::OutOfPossession};

  constexpr std::size_t kWinger = 6;
  auto narrow = Boards(home, away);
  auto wide = Boards(home, away);
  Plan(narrow, TeamSide::Home).in_possession.attacking_width = 0.2f;
  Plan(wide, TeamSide::Home).in_possession.attacking_width = 0.95f;
  const auto narrow_result = Interpret(narrow, assignments, world, attacking);
  const auto wide_result = Interpret(wide, assignments, world, attacking);
  CHECK(std::abs(wide_result.players[0][kWinger].target_anchor_metres.coords[1]) >
        std::abs(narrow_result.players[0][kWinger].target_anchor_metres.coords[1]));
}

TEST_CASE("individual instructions override role and team defaults",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = Boards(home, away);
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();
  InterpretationState state;
  state.phase = {TacticalPhase::InPossession, TacticalPhase::OutOfPossession};

  constexpr std::size_t kStriker = 9;
  Plan(boards, TeamSide::Home).in_possession.attacking_bias = 0.9f;
  Plan(boards, TeamSide::Home).slots[kStriker].instructions.forward_runs = 0.0f;
  Plan(boards, TeamSide::Home).slots[kStriker].instructions.positional_freedom =
      0.1f;
  const auto result = Interpret(boards, assignments, world, state);
  CHECK(result.players[0][kStriker].forward_run_bias == Approx(0.0f));
  CHECK(result.players[0][kStriker].positional_freedom == Approx(0.1f));
  // The team instruction itself is not rewritten.
  CHECK(result.teams[0].tempo == Approx(0.5f));
}

TEST_CASE("man marking is exclusive and zonal ignores the target",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();
  InterpretationState state;
  state.phase = {TacticalPhase::OutOfPossession, TacticalPhase::OutOfPossession};

  SECTION("zonal ignores plan marking targets") {
    auto boards = Boards(home, away);
    auto& plan = Plan(boards, TeamSide::Home);
    plan.out_of_possession.marking = MarkingStyle::Zonal;
    plan.slots[2].instructions.marking_target = 205;
    const auto result = Interpret(boards, assignments, world, state);
    CHECK_FALSE(result.players[0][2].mark_target.has_value());
    CHECK_FALSE(result.teams[0].marking_conflict);
  }
  SECTION("man marking with a duplicate target is flagged") {
    auto boards = Boards(home, away);
    auto& plan = Plan(boards, TeamSide::Home);
    plan.out_of_possession.marking = MarkingStyle::Man;
    plan.slots[2].instructions.marking_target = 205;
    plan.slots[3].instructions.marking_target = 205;
    const auto result = Interpret(boards, assignments, world, state);
    CHECK(result.players[0][2].mark_target.has_value());
    CHECK(result.teams[0].marking_conflict);
  }
}

TEST_CASE("a second-half defending direction stays consistent",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = Boards(home, away);
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();
  // Change of ends: the observed defending directions swap.
  world.teams[0].defending_direction = 1;
  world.teams[1].defending_direction = -1;

  InterpretationState state;
  state.phase = {TacticalPhase::InPossession, TacticalPhase::OutOfPossession};
  const auto result = Interpret(boards, assignments, world, state);
  CHECK(result.players[0][9].target_anchor_metres.coords[0] < 0.0f);
  CHECK(result.players[1][9].target_anchor_metres.coords[0] > 0.0f);
}

TEST_CASE("legacy boards are reported unsupported",
          "[ai][tactical][interpreter]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  std::array<TacticalBoard, 2> boards;
  boards[0] = football::ai::MakeTacticalBoard(home, TeamSide::Home, world_pitch());
  boards[1] = football::ai::MakeTacticalBoard(away, TeamSide::Away, world_pitch());
  const auto assignments = Assign(home, away);
  WorldState world = MakeWorld();
  const auto result = Interpret(boards, assignments, world, {});
  CHECK_FALSE(result.teams[0].has_plan);
  CHECK_FALSE(result.teams[1].has_plan);
}

TEST_CASE("Runtime formation slot bindings survive vacancies and replacement", "[team][tactical]") {
  WorldState world;
  WorldPlayerState first;
  first.id = 90; first.active = true;
  WorldPlayerState second;
  second.id = 12; second.active = true;
  WorldPlayerState substitute;
  substitute.id = 70; substitute.active = false;
  world.players = {second,first,substitute}; // not slot order
  world.active_lineups.emplace();
  (*world.active_lineups)[0].slots[0] = 90;
  (*world.active_lineups)[0].slots[8] = 12;
  auto assignment = football::ai::DefaultSlotAssignment(world,TeamSide::Home);
  CHECK(assignment.players[0] == 90);
  CHECK(assignment.players[8] == 12);
  CHECK_FALSE(assignment.players[1]);
  world.players[1].active = false;
  assignment = football::ai::DefaultSlotAssignment(world,TeamSide::Home);
  CHECK_FALSE(assignment.players[0]);
  CHECK(assignment.players[8] == 12);
  (*world.active_lineups)[0].slots[8] = 70; // projected Runtime substitution
  world.players[0].active = false;
  world.players[2].active = true;
  assignment = football::ai::DefaultSlotAssignment(world,TeamSide::Home);
  CHECK(assignment.players[8] == 70);
  CHECK_FALSE(assignment.players[0]);
}
