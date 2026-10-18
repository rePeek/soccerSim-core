#include <catch2/catch_test_macros.hpp>
#include "testing/simulation_access.hpp"
#include "runtime/formation_build.hpp"

using namespace football::sim;
using football::sim::testing::SimulationAccess;
namespace {
TeamSetup OwnershipSetup(unsigned first) {
  TeamSetup s;
  for (unsigned i = 0; i < 2; ++i) {
    football::player::PlayerDefinition p;
    p.id = first + i;
    p.height = 1.75f + i * .1f;
    s.players.push_back(p);
    s.formation.push_back({{-.7f + i * .4f, i * .3f},
        i == 0 ? e_PlayerRole_GK : e_PlayerRole_CF});
  }
  return s;
}
}
TEST_CASE("Team ownership preserves legacy formation adapter", "[team][ownership]") {
  auto s = OwnershipSetup(50);
  const auto legacy = BuildFormation(s);
  const auto migrated = BuildFormation(MakeTeam(s));
  REQUIRE(legacy.size() == migrated.size());
  for (std::size_t i = 0; i < legacy.size(); ++i) {
    CHECK(legacy[i].position == migrated[i].position);
    CHECK(legacy[i].start_position == migrated[i].start_position);
    CHECK(legacy[i].role == migrated[i].role);
    CHECK(legacy[i].lazy == migrated[i].lazy);
    CHECK(legacy[i].controllable == migrated[i].controllable);
  }
}
TEST_CASE("Player models survive caller teardown and Stop/re-Init", "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  for (bool reverse : {false,true}) {
    auto home = OwnershipSetup(50);
    auto away = OwnershipSetup(500);
    options.reverse_team_processing = reverse;
    sim.Init(home,away,football::pitch::Pitch{},options);
    // Caller definitions are initialization input only, never actor borrows.
    home.players.clear();
    home.players.shrink_to_fit();
    for (int side = 0; side < 2; ++side) {
      const auto& team = SimulationAccess::TeamOf(sim,static_cast<TeamSide>(side));
      const auto actors = SimulationAccess::RosterOf(sim,side).Players();
      REQUIRE(actors.size() == team.players.size());
      for (std::size_t i = 0; i < actors.size(); ++i) {
        CHECK(&actors[i]->GetModel() != &team.players[i]);
        CHECK(actors[i]->GetModel().height == team.players[i].height);
        CHECK(actors[i]->GetModel().attributes == team.players[i].attributes);
        CHECK(actors[i]->GetID() == (side == 0 ? 50 : 500) + i);
        CHECK(actors[i]->GetTeamSide() == static_cast<TeamSide>(side));
      }
    }
    CHECK_THROWS_AS(sim.Init(away,away,football::pitch::Pitch{},options),std::logic_error);
    CHECK(sim.Stop());
    CHECK_FALSE(sim.Stop());
  }
  auto invalid = OwnershipSetup(50);
  invalid.players[1].id = invalid.players[0].id;
  CHECK_THROWS_AS(sim.Init(invalid,OwnershipSetup(500),football::pitch::Pitch{},options),std::invalid_argument);
  CHECK(SimulationAccess::RosterOf(sim,0).Players().empty());
  CHECK_NOTHROW(sim.Init(OwnershipSetup(50),OwnershipSetup(500),football::pitch::Pitch{},options));
  CHECK(sim.Stop());
}
TEST_CASE("Failed Init destroys actors before definitions can be replaced", "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  // vector::max_size rejects this synchronously, after both rosters have been
  // constructed but before publication. No huge allocation or simulation loop.
  options.snapshot_capacity = std::numeric_limits<std::size_t>::max();
  CHECK_THROWS_AS(sim.Init(OwnershipSetup(50),OwnershipSetup(500),
      football::pitch::Pitch{},options),std::length_error);
  CHECK(SimulationAccess::RosterOf(sim,0).Players().empty());
  CHECK(SimulationAccess::RosterOf(sim,1).Players().empty());
  CHECK_FALSE(sim.Stop());
  options.snapshot_capacity = 8;
  REQUIRE_NOTHROW(sim.Init(OwnershipSetup(800),OwnershipSetup(900),
      football::pitch::Pitch{},options));
  const auto players = SimulationAccess::RosterOf(sim,0).Players();
  const auto& team = SimulationAccess::TeamOf(sim,TeamSide::Home);
  CHECK(players.front()->GetModel().height == team.players.front().height);
  CHECK(players.front()->GetID() == 800);
  CHECK(sim.Stop());
}
TEST_CASE("Explicit lineup binds identities to formation slots not declaration indices", "[team][ownership]") {
  auto home = OwnershipSetup(50);
  auto bench = home.players.front();
  bench.id = 52;
  home.players.push_back(bench);
  home.lineup.emplace();
  home.lineup->starters[0] = 51;
  home.lineup->starters[8] = 50;
  home.lineup->substitutes = {52};
  home.formation.resize(9);
  home.formation[8] = {{.2f,.3f},e_PlayerRole_CF};
  home.leadership = {51,{50}};
  home.set_piece_orders.penalties = {52,50};
  MatchOptions options;
  options.snapshot_capacity = 8;
  Simulation sim;
  sim.Init(home,OwnershipSetup(500),football::pitch::Pitch{},options);
  const auto players = SimulationAccess::RosterOf(sim,0).Players();
  REQUIRE(players.size() == 3);
  CHECK(players[0]->GetID() == 50);
  CHECK(players[0]->GetFormationEntry().role == e_PlayerRole_CF);
  CHECK(players[1]->GetID() == 51);
  CHECK(players[1]->GetFormationEntry().role == e_PlayerRole_GK);
  CHECK_FALSE(players[2]->IsActive());
  auto world = sim.Observe();
  REQUIRE(world.active_lineups);
  CHECK((*world.active_lineups)[0].slots[0] == 51);
  CHECK((*world.active_lineups)[0].slots[8] == 50);
  SimulationAccess::SendOff(sim,*players[1]);
  world = sim.Observe();
  CHECK_FALSE((*world.active_lineups)[0].slots[0]);
  CHECK((*world.active_lineups)[0].slots[8] == 50);
  const auto& team = SimulationAccess::TeamOf(sim,TeamSide::Home);
  CHECK(team.initial_lineup.starters[0] == 51);
  const std::vector<EligiblePlayer> eligible{{50,true},{51,false},{52,false}};
  CHECK(SelectCaptain(team.leadership,eligible,SelectionFallback::CandidateOrder) == 50);
  CHECK(SelectEligibleTaker(team.set_piece_orders.penalties,eligible,SelectionFallback::None) == 50);
  CHECK(sim.Stop());
}
TEST_CASE("Orders and reusable presets are inert until restart integration", "[team][ownership]") {
  const auto home = OwnershipSetup(50);
  const auto away = OwnershipSetup(500);
  auto configured = home;
  configured.leadership = {51,{50}};
  configured.set_piece_orders.penalties = {51,50};
  configured.formation_presets.presets = {{"kickoff_custom",
      FormationPresetKind::KickOff,FormationPresetSide::Attacking,
      {{1,{AnchorFrame::TeamRelative,.4f,.2f},"receiver"}}}};
  MatchOptions options;
  options.snapshot_capacity = 8;
  Simulation legacy, migrated;
  legacy.Init(home,away,football::pitch::Pitch{},options);
  migrated.Init(configured,away,football::pitch::Pitch{},options);
  const auto compare = [&] {
    const auto* a = legacy.Snapshots().Latest();
    const auto* b = migrated.Snapshots().Latest();
    REQUIRE(a); REQUIRE(b);
    CHECK(a->snapshot.ball.position == b->snapshot.ball.position);
    CHECK(a->snapshot.ball.velocity == b->snapshot.ball.velocity);
    REQUIRE(a->snapshot.players.size() == b->snapshot.players.size());
    for (std::size_t i = 0; i < a->snapshot.players.size(); ++i) {
      CHECK(a->snapshot.players[i].position == b->snapshot.players[i].position);
      CHECK(a->snapshot.players[i].velocity == b->snapshot.players[i].velocity);
      CHECK(a->snapshot.players[i].animation_id == b->snapshot.players[i].animation_id);
      CHECK(a->snapshot.players[i].active == b->snapshot.players[i].active);
    }
    CHECK(SimulationAccess::RngOf(legacy).engine() == SimulationAccess::RngOf(migrated).engine());
    CHECK(legacy.Observe().restart_taker == migrated.Observe().restart_taker);
  };
  compare();
  // Only one bounded ceremonial tick; no full-match loop or long new scenario.
  legacy.Step(PlayerControlSet{});
  migrated.Step(PlayerControlSet{});
  compare();
  CHECK(legacy.Stop());
  CHECK(migrated.Stop());
}
TEST_CASE("Player owns an immutable definition after its source is destroyed", "[team][ownership]") {
  AnimationLibrary animations;
  const football::pitch::Pitch pitch;
  football::math::Rng rng;
  const auto before = rng.engine();
  std::unique_ptr<Player> actor;
  {
    football::player::PlayerDefinition source;
    source.id = 4321; source.first_name = "Own"; source.last_name = "Model";
    source.age = 28; source.height = 1.9f; source.weight_kg = 80.f;
    source.attributes.set(football::player::PlayerStat::mental_vision,.6f);
    source.appearance.skin_color = 3;
    source.appearance.hair_style = "short02";
    source.appearance.hair_color = "black";
    actor = std::make_unique<Player>(0,TeamSide::Home,1.f,source,FormationEntry{},
        7,animations,pitch,rng);
    CHECK(&actor->GetModel() != &source);
    source.id = 0; source.height = 0; source.first_name.clear();
    source.attributes.fill(0);
  }
  const auto& model = actor->GetModel();
  CHECK(actor->GetID() == 4321);
  CHECK(model.first_name == "Own"); CHECK(model.last_name == "Model");
  CHECK(model.age == 28); CHECK(model.height == 1.9f); CHECK(model.weight_kg == 80.f);
  CHECK(model.attributes.get(football::player::PlayerStat::mental_vision) == .6f);
  CHECK(model.appearance.skin_color == 3);
  CHECK(model.appearance.hair_style == "short02");
  CHECK(model.appearance.hair_color == "black");
  CHECK(actor->BodyProfile().height_m == 1.9f);
  CHECK(actor->BodyProfile().mass_kg == 80.f);
  CHECK(rng.engine() == before);
}
