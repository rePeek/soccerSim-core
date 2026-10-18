#include <catch2/catch_test_macros.hpp>
#include "runtime/team_preparation.hpp"
#include "football/runtime/match_config.hpp"
#include <limits>

using namespace football::sim;
namespace {
TeamSetup Setup() {
  TeamSetup s;
  s.name = "Team";
  for (auto id : {90u, 12u, 70u}) {
    football::player::PlayerDefinition p;
    p.id = id;
    s.players.push_back(p);
  }
  return s;
}
}
TEST_CASE("Team conversion retains declarations and legacy formation count", "[team]") {
  auto setup = Setup();
  auto t = PrepareTeam(setup);
  CHECK(t.name == setup.name);
  CHECK(t.players[0].id == 90);
  CHECK(t.initial_lineup.starters[0] == 90);
  CHECK(t.initial_lineup.starters[2] == 70);
  CHECK_FALSE(t.initial_lineup.starters[3]);
  CHECK_FALSE(t.leadership.captain);
  CHECK(t.set_piece_orders.penalties.empty());
  CHECK(t.formation_presets.presets.empty());
  setup.formation.resize(1);
  t = PrepareTeam(setup);
  CHECK_FALSE(t.initial_lineup.starters[1]);
  CHECK(t.initial_lineup.substitutes == std::vector<football::player::PlayerId>{12,70});
  setup = Setup();
  for (unsigned id = 100; id < 112; ++id) {
    football::player::PlayerDefinition p;
    p.id = id;
    setup.players.push_back(p);
  }
  t = PrepareTeam(setup);
  CHECK(t.initial_lineup.starters[10] == 107);
  CHECK(t.initial_lineup.substitutes.size() == 4);
}
TEST_CASE("Team validates identity and explicit sparse lineup", "[team]") {
  auto s = Setup();
  s.lineup.emplace();
  s.lineup->starters[8] = 70;
  s.lineup->starters[0] = 12;
  s.lineup->substitutes = {90};
  auto t = PrepareTeam(s);
  CHECK(t.explicit_lineup);
  CHECK(t.initial_lineup.starters[8] == 70);
  CHECK_FALSE(t.initial_lineup.starters[1]);
  SECTION("duplicate starter") { s.lineup->starters[1] = 70; }
  SECTION("starter also substitute") { s.lineup->substitutes.push_back(70); }
  SECTION("unknown identity") { s.lineup->starters[1] = 400; }
  SECTION("duplicate definition") { s.players[0].id = 12; }
  SECTION("invalid identity") { s.players[0].id = football::player::kInvalidPlayerId; }
  CHECK_THROWS_AS(PrepareTeam(s), std::invalid_argument);
}
TEST_CASE("Team match identities cannot overlap", "[team]") {
  auto t = PrepareTeam(Setup());
  CHECK_THROWS_AS(ValidateMatchTeams({t,t}), std::invalid_argument);
}
TEST_CASE("Leadership and taker orders filter eligibility without ID ranking", "[team]") {
  const std::vector<EligiblePlayer> candidates{{90, false}, {70, true}, {12, true}};
  const std::vector<football::player::PlayerId> priority{90,12};
  CHECK(SelectEligibleTaker(priority,candidates,SelectionFallback::None) == 12);
  CHECK(SelectEligibleTaker({},candidates,SelectionFallback::CandidateOrder) == 70);
  CHECK_FALSE(SelectEligibleTaker({},candidates,SelectionFallback::None));
  CHECK_FALSE(SelectEligibleTaker(priority,{},SelectionFallback::CandidateOrder));
  const std::vector<football::player::PlayerId> exhausted{90};
  CHECK(SelectEligibleTaker(exhausted,candidates,SelectionFallback::CandidateOrder) == 70);
  CHECK_FALSE(SelectEligibleTaker(exhausted,candidates,SelectionFallback::None));
  const std::vector<EligiblePlayer> unavailable{{90,false},{12,false}};
  CHECK_FALSE(SelectEligibleTaker(priority,unavailable,SelectionFallback::CandidateOrder));
  CHECK(SelectCaptain({90,{12,70}},candidates,SelectionFallback::CandidateOrder) == 12);
  CHECK(SelectCaptain({90,{}},candidates,SelectionFallback::CandidateOrder) == 70);
  auto s = Setup();
  SECTION("duplicate priority") { s.set_piece_orders.penalties = {90,90}; }
  SECTION("unknown priority") { s.set_piece_orders.corners_left = {999}; }
  SECTION("unknown captain") { s.leadership.captain = 999; }
  SECTION("duplicate vice captain") { s.leadership.vice_captains = {12,12}; }
  CHECK_THROWS_AS(PrepareTeam(s),std::invalid_argument);
}
TEST_CASE("Runtime slot vacancy and substitution preserve template identity", "[team]") {
  const auto t = PrepareTeam(Setup());
  ActiveLineupState active{t.initial_lineup.starters};
  VacateSlot(active,12);
  CHECK_FALSE(active.slots[1]);
  CHECK(active.slots[2] == 70);
  ReplaceSlot(active,1,500);
  CHECK(active.slots[1] == 500);
  CHECK(t.initial_lineup.starters[1] == 12);
  CHECK_THROWS_AS(ReplaceSlot(active,2,500),std::invalid_argument);
  CHECK_THROWS_AS(ReplaceSlot(active,11,40),std::invalid_argument);
}
TEST_CASE("Formation presets reject ambiguity and malformed anchors", "[team]") {
  auto s = Setup();
  s.formation_presets.presets.push_back({"kickoff",FormationPresetKind::KickOff,
      FormationPresetSide::Attacking, {{0,{AnchorFrame::TeamRelative,0,0},"keeper"}}});
  CHECK_NOTHROW(PrepareTeam(s));
  auto& p = s.formation_presets.presets.front();
  SECTION("duplicate id") { s.formation_presets.presets.push_back(p); }
  SECTION("duplicate slot") { p.positions.push_back(p.positions.front()); }
  SECTION("invalid slot") { p.positions[0].slot = 11; }
  SECTION("nonfinite") { p.positions[0].anchor.x = std::numeric_limits<float>::infinity(); }
  SECTION("normalized bound") { p.positions[0].anchor.y = 1.01f; }
  SECTION("unknown frame") { p.positions[0].anchor.frame = static_cast<AnchorFrame>(42); }
  SECTION("empty id") { p.id.clear(); }
  SECTION("unknown kind") { p.kind = static_cast<FormationPresetKind>(42); }
  SECTION("unknown side") { p.side = static_cast<FormationPresetSide>(42); }
  SECTION("negative normalized x") { p.positions[0].anchor.x = -.01f; }
  CHECK_THROWS_AS(PrepareTeam(s), std::invalid_argument);
}
TEST_CASE("Role categories may share a nonstarter but not duplicate within a category", "[team]") {
  auto s = Setup();
  s.formation.resize(1);
  s.set_piece_orders.penalties = {70};
  s.set_piece_orders.direct_free_kicks = {70};
  s.set_piece_orders.indirect_free_kicks = {70};
  s.set_piece_orders.corners_left = {70};
  s.set_piece_orders.corners_right = {70};
  s.set_piece_orders.throw_ins = {70};
  CHECK_NOTHROW(PrepareTeam(s));
}
