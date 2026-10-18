// TACT-1: TacticalPlanV2 value schema, validation and deterministic bootstrap.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>

#include "football/ai/tactical_board.hpp"
#include "football/ai/tactical_plan.hpp"

namespace {

using football::ai::BoardMode;
using football::ai::MakeTacticalBoard;
using football::ai::MakeTacticalBoardV2;
using football::ai::MakeTacticalPlanV2;
using football::ai::TacticalAnchor;
using football::ai::TacticalErrorCode;
using football::ai::TacticalPlanV2;
using football::ai::ValidateTacticalBoard;
using football::ai::ValidateTacticalPlan;

football::sim::TeamSetup MakeTeam() {
  football::sim::TeamSetup team;
  team.name = "schema";
  constexpr e_PlayerRole kRoles[11] = {
      e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
      e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
      e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};
  for (int i = 0; i < 11; ++i) {
    football::player::PlayerDefinition definition;
    definition.id = static_cast<football::player::PlayerId>(100 + i);
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

}  // namespace

TEST_CASE("a default plan is structurally valid", "[ai][tactical][schema]") {
  TacticalPlanV2 plan;
  for (std::size_t i = 0; i < plan.slots.size(); ++i)
    plan.slots[i].id = static_cast<football::ai::TacticalSlotId>(i);
  const auto result = ValidateTacticalPlan(plan);
  CHECK(result.ok);
  CHECK(result.error == TacticalErrorCode::Ok);
}

TEST_CASE("slot identity is validated", "[ai][tactical][schema]") {
  TacticalPlanV2 plan;
  for (std::size_t i = 0; i < plan.slots.size(); ++i)
    plan.slots[i].id = static_cast<football::ai::TacticalSlotId>(i);

  SECTION("out of range id") {
    plan.slots[3].id = 11;
    const auto result = ValidateTacticalPlan(plan);
    REQUIRE_FALSE(result.ok);
    CHECK(result.error == TacticalErrorCode::InvalidSlotId);
    CHECK(result.slot_index == 3);
  }
  SECTION("duplicate id") {
    plan.slots[3].id = plan.slots[2].id;
    const auto result = ValidateTacticalPlan(plan);
    REQUIRE_FALSE(result.ok);
    CHECK(result.error == TacticalErrorCode::DuplicateSlotId);
  }
}

TEST_CASE("numeric instructions reject non-finite and out-of-range",
          "[ai][tactical][schema]") {
  TacticalPlanV2 plan;
  for (std::size_t i = 0; i < plan.slots.size(); ++i)
    plan.slots[i].id = static_cast<football::ai::TacticalSlotId>(i);

  SECTION("team value above one") {
    plan.in_possession.tempo = 1.5f;
    CHECK(ValidateTacticalPlan(plan).error == TacticalErrorCode::ValueOutOfRange);
  }
  SECTION("nan team value") {
    plan.out_of_possession.defensive_line = std::numeric_limits<float>::quiet_NaN();
    CHECK(ValidateTacticalPlan(plan).error == TacticalErrorCode::ValueOutOfRange);
  }
  SECTION("anchor out of range") {
    plan.slots[4].in_possession.u = -0.01f;
    const auto result = ValidateTacticalPlan(plan);
    REQUIRE_FALSE(result.ok);
    CHECK(result.slot_index == 4);
  }
  SECTION("individual unset is allowed, explicit zero is allowed") {
    plan.slots[5].instructions.forward_runs = std::nullopt;
    CHECK(ValidateTacticalPlan(plan).ok);
    plan.slots[5].instructions.forward_runs = 0.0f;
    CHECK(ValidateTacticalPlan(plan).ok);
  }
  SECTION("individual out of range") {
    plan.slots[5].instructions.pressing = 1.2f;
    const auto result = ValidateTacticalPlan(plan);
    REQUIRE_FALSE(result.ok);
    CHECK(result.slot_index == 5);
  }
  SECTION("invalid marking target") {
    plan.slots[6].instructions.marking_target =
        football::player::kInvalidPlayerId;
    CHECK(ValidateTacticalPlan(plan).error ==
          TacticalErrorCode::InvalidMarkingTarget);
  }
}

TEST_CASE("MakeTacticalPlanV2 is deterministic and well formed",
          "[ai][tactical][schema]") {
  const auto team = MakeTeam();
  const football::pitch::Pitch pitch;
  const auto first = MakeTacticalPlanV2(team, football::sim::TeamSide::Home, pitch);
  const auto second = MakeTacticalPlanV2(team, football::sim::TeamSide::Home, pitch);
  REQUIRE(ValidateTacticalPlan(first).ok);

  CHECK(first.slots.size() == 11);
  for (std::size_t i = 0; i < first.slots.size(); ++i) {
    CHECK(first.slots[i].id == i);
    // Both phases start identical.
    CHECK(first.slots[i].in_possession.u == first.slots[i].out_of_possession.u);
    CHECK(first.slots[i].in_possession.v == first.slots[i].out_of_possession.v);
    CHECK(first.slots[i].in_possession.u == second.slots[i].in_possession.u);
    CHECK(first.slots[i].in_possession.v == second.slots[i].in_possession.v);
  }
  // The home side is independent of side argument for the canonical plan.
  const auto away = MakeTacticalPlanV2(team, football::sim::TeamSide::Away, pitch);
  CHECK(away.slots[9].in_possession.u == first.slots[9].in_possession.u);
  CHECK(away.slots[9].in_possession.v == first.slots[9].in_possession.v);
}

TEST_CASE("board mode and advanced plan stay paired", "[ai][tactical][schema]") {
  const auto team = MakeTeam();
  const football::pitch::Pitch pitch;

  const auto legacy = MakeTacticalBoard(team, football::sim::TeamSide::Home, pitch);
  CHECK(legacy.mode == BoardMode::Legacy);
  CHECK_FALSE(legacy.advanced.has_value());
  CHECK(ValidateTacticalBoard(legacy));
  CHECK(legacy.players.size() == 11);

  const auto v2 = MakeTacticalBoardV2(team, football::sim::TeamSide::Home, pitch);
  CHECK(v2.mode == BoardMode::V2);
  REQUIRE(v2.advanced.has_value());
  CHECK(ValidateTacticalBoard(v2));
  // The legacy fields are not populated in V2 mode.
  CHECK(v2.players.empty());
  CHECK(v2.width == 0.75f);
  CHECK(v2.depth == 0.55f);
  CHECK(football::ai::AdvancedPlan(v2) != nullptr);

  // A half-built board is rejected: V2 without a plan, or a plan without V2.
  football::ai::TacticalBoard broken;
  broken.mode = BoardMode::V2;
  CHECK_FALSE(ValidateTacticalBoard(broken));
  broken.mode = BoardMode::Legacy;
  broken.advanced =
      MakeTacticalPlanV2(team, football::sim::TeamSide::Home, pitch);
  CHECK_FALSE(ValidateTacticalBoard(broken));
}
