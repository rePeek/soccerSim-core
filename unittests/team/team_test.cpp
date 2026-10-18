#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

#include "football/team/team.hpp"

using football::player::PlayerDefinition;
using football::player::PlayerId;
using namespace football::team;

namespace {
TeamDefinition MakeTeamDefinition(std::initializer_list<PlayerId> ids) {
  TeamDefinition definition;
  definition.name = "Team";
  for (auto id : ids) {
    PlayerDefinition player;
    player.id = id;
    definition.players.push_back(player);
  }
  std::size_t slot = 0;
  for (auto id : ids) {
    if (slot < kFormationSlots) definition.lineup.starters[slot++] = id;
  }
  return definition;
}
}

TEST_CASE("TeamDefinition validation owns static identity and lineup invariants",
          "[team]") {
  auto team = MakeTeamDefinition({90, 12, 70});
  CHECK_NOTHROW(ValidateTeamDefinition(team));
  CHECK(team.lineup.starters[0] == 90);
  CHECK(team.lineup.starters[2] == 70);
  CHECK_FALSE(team.lineup.starters[3]);

  SECTION("empty squad") { team.players.clear(); }
  SECTION("invalid identity") {
    team.players[0].id = football::player::kInvalidPlayerId;
  }
  SECTION("duplicate identity") { team.players[0].id = 12; }
  SECTION("unknown lineup identity") { team.lineup.starters[1] = 400; }
  SECTION("duplicate starter") { team.lineup.starters[1] = 90; }
  SECTION("starter also substitute") { team.lineup.substitutes.push_back(90); }
  SECTION("formation exceeds slots") { team.initial_formation.resize(12); }
  CHECK_THROWS_AS(ValidateTeamDefinition(team), std::invalid_argument);
}

TEST_CASE("TeamDefinition appointments and orders validate membership and uniqueness",
          "[team]") {
  auto team = MakeTeamDefinition({90, 12, 70});
  team.leadership = {90, {12, 70}};
  team.set_piece_orders.penalties = {12, 70};
  team.set_piece_orders.direct_free_kicks = {70, 12};
  CHECK_NOTHROW(ValidateTeamDefinition(team));

  SECTION("unknown captain") { team.leadership.captain = 999; }
  SECTION("captain also vice captain") { team.leadership.vice_captains = {90}; }
  SECTION("duplicate vice captain") { team.leadership.vice_captains = {12, 12}; }
  SECTION("unknown taker") { team.set_piece_orders.penalties = {999}; }
  SECTION("duplicate category") { team.set_piece_orders.penalties = {12, 12}; }
  CHECK_THROWS_AS(ValidateTeamDefinition(team), std::invalid_argument);
}

TEST_CASE("TeamDefinition categories may share a taker but not duplicate within a category",
          "[team]") {
  auto team = MakeTeamDefinition({90, 12, 70});
  team.set_piece_orders.penalties = {70};
  team.set_piece_orders.direct_free_kicks = {70};
  team.set_piece_orders.indirect_free_kicks = {70};
  team.set_piece_orders.corners_left = {70};
  team.set_piece_orders.corners_right = {70};
  team.set_piece_orders.throw_ins = {70};
  CHECK_NOTHROW(ValidateTeamDefinition(team));
}

TEST_CASE("Cross-team PlayerId identities are unique", "[team]") {
  const auto team = MakeTeamDefinition({90, 12, 70});
  CHECK_THROWS_AS(ValidateTeamDefinitions(team, team), std::invalid_argument);
  auto other = MakeTeamDefinition({100, 101, 102});
  CHECK_NOTHROW(ValidateTeamDefinitions(team, other));
}

TEST_CASE("Leadership and taker selection are deterministic and eligibility driven",
          "[team]") {
  const std::vector<EligiblePlayer> candidates{{90, false}, {70, true}, {12, true}};
  const std::vector<PlayerId> priority{90, 12};
  CHECK(SelectEligibleTaker(priority, candidates, SelectionFallback::None) == 12);
  CHECK(SelectEligibleTaker({}, candidates, SelectionFallback::CandidateOrder) == 70);
  CHECK_FALSE(SelectEligibleTaker({}, candidates, SelectionFallback::None));
  CHECK_FALSE(SelectEligibleTaker(priority, {}, SelectionFallback::CandidateOrder));
  const std::vector<PlayerId> exhausted{90};
  CHECK(SelectEligibleTaker(exhausted, candidates,
                            SelectionFallback::CandidateOrder) == 70);
  CHECK_FALSE(SelectEligibleTaker(exhausted, candidates,
                                  SelectionFallback::None));
  const std::vector<EligiblePlayer> unavailable{{90, false}, {12, false}};
  CHECK_FALSE(SelectEligibleTaker(priority, unavailable,
                                  SelectionFallback::CandidateOrder));
  CHECK(SelectCaptain({90, {12, 70}}, candidates,
                      SelectionFallback::CandidateOrder) == 12);
  CHECK(SelectCaptain({90, {}}, candidates,
                      SelectionFallback::CandidateOrder) == 70);
}

TEST_CASE("TeamDefinition is a copyable static value with no live actor state",
          "[team]") {
  auto team = MakeTeamDefinition({90, 12, 70});
  team.name = "Copyable";
  const auto copy = team;
  CHECK(copy.name == "Copyable");
  CHECK(copy.players.size() == 3);
  CHECK(copy.lineup.starters[0] == 90);
}

TEST_CASE("Formation presets validate ambiguity and malformed anchors", "[team]") {
  auto team = MakeTeamDefinition({90, 12, 70});
  team.formations.presets.push_back({"kickoff", FormationPresetKind::KickOff,
      FormationPresetSide::Attacking,
      {{0, {AnchorFrame::TeamRelative, 0, 0}, "keeper"}}});
  CHECK_NOTHROW(ValidateTeamDefinition(team));
  auto& preset = team.formations.presets.front();
  SECTION("duplicate id") { team.formations.presets.push_back(preset); }
  SECTION("duplicate slot") { preset.positions.push_back(preset.positions.front()); }
  SECTION("invalid slot") { preset.positions[0].slot = 11; }
  SECTION("nonfinite") {
    preset.positions[0].anchor.x = std::numeric_limits<float>::infinity();
  }
  SECTION("normalized bound") { preset.positions[0].anchor.y = 1.01f; }
  SECTION("unknown frame") {
    preset.positions[0].anchor.frame = static_cast<AnchorFrame>(42);
  }
  SECTION("empty id") { preset.id.clear(); }
  SECTION("unknown kind") {
    preset.kind = static_cast<FormationPresetKind>(42);
  }
  SECTION("unknown side") {
    preset.side = static_cast<FormationPresetSide>(42);
  }
  SECTION("negative normalized x") { preset.positions[0].anchor.x = -.01f; }
  CHECK_THROWS_AS(ValidateTeamDefinition(team), std::invalid_argument);
}
