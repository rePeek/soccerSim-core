#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <vector>
#include <string>

#include "runtime/formation_build.hpp"
#include "runtime/team_setup_adapter.hpp"
#include "testing/simulation_access.hpp"

using namespace football::sim;
using football::sim::testing::SimulationAccess;

namespace {
TeamSetup OwnershipSetup(unsigned first) {
  TeamSetup setup;
  setup.name = "Team " + std::to_string(first);
  for (unsigned i = 0; i < 2; ++i) {
    football::player::PlayerDefinition player;
    player.id = first + i;
    player.height = 1.75f + i * .1f;
    setup.players.push_back(player);
    setup.formation.push_back({{-.7f + i * .4f, i * .3f},
        i == 0 ? e_PlayerRole_GK : e_PlayerRole_CF});
  }
  return setup;
}
}  // namespace

TEST_CASE("Runtime formation adaptation consumes static Team configuration",
          "[team][ownership]") {
  auto setup = OwnershipSetup(50);
  auto prepared = PrepareTeam(setup);
  CHECK_FALSE(prepared.explicit_lineup);
  const auto formation = BuildFormation(prepared.definition, prepared.explicit_lineup);
  REQUIRE(formation.size() == 2);
  CHECK(formation[0].role == e_PlayerRole_GK);
  CHECK(formation[1].role == e_PlayerRole_CF);
  CHECK(formation[0].start_position ==
        Vector3(setup.formation[0].position.x,
                setup.formation[0].position.y * FORMATION_Y_SCALE, 0));
  CHECK(formation[1].start_position ==
        Vector3(setup.formation[1].position.x,
                setup.formation[1].position.y * FORMATION_Y_SCALE, 0));
}

TEST_CASE("Runtime separates static Team from Player and live team state",
          "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  for (bool reverse : {false, true}) {
    auto home = OwnershipSetup(50);
    auto away = OwnershipSetup(500);
    options.reverse_team_processing = reverse;
    sim.Init(home, away, football::pitch::Pitch{}, options);
    // Caller declarations and Player definitions have independent lifetimes.
    home.players.clear();
    home.players.shrink_to_fit();
    for (int side = 0; side < 2; ++side) {
      const auto& team = SimulationAccess::DefinitionOf(
          sim, static_cast<TeamSide>(side));
      const auto actors = SimulationAccess::TeamOf(sim, side).Players();
      REQUIRE(actors.size() == 2);
      REQUIRE(team.players.size() == actors.size());
      CHECK(SimulationAccess::TeamViewOf(sim, side).players.data() ==
            actors.data());
      CHECK(SimulationAccess::ActiveLineupOf(sim, side).slots ==
            team.lineup.starters);
      CHECK(SimulationAccess::TeamViewOf(sim, side).frame ==
            &SimulationAccess::TeamFrameOf(sim, side));
      CHECK(SimulationAccess::TeamViewOf(sim, side).state ==
            &SimulationAccess::TeamStateOf(sim, side));
      for (std::size_t i = 0; i < actors.size(); ++i) {
        CHECK(&actors[i]->GetModel() != &team.players[i]);
        CHECK(actors[i]->GetModel().height == team.players[i].height);
        CHECK(actors[i]->GetID() == (side == 0 ? 50u : 500u) + i);
        CHECK(actors[i]->GetTeamSide() == static_cast<TeamSide>(side));
      }
    }
    CHECK_THROWS_AS(sim.Init(away, away, football::pitch::Pitch{}, options),
                    std::logic_error);
    CHECK(sim.Stop());
    CHECK_FALSE(sim.Stop());
  }
  auto invalid = OwnershipSetup(50);
  invalid.players[1].id = invalid.players[0].id;
  CHECK_THROWS_AS(sim.Init(invalid, OwnershipSetup(500),
                           football::pitch::Pitch{}, options),
                  std::invalid_argument);
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim, TeamSide::Home),
                  std::logic_error);
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim, 0), std::logic_error);
  CHECK_NOTHROW(sim.Init(OwnershipSetup(50), OwnershipSetup(500),
                         football::pitch::Pitch{}, options));
  CHECK(sim.Stop());
}

TEST_CASE("Failed Init destroys actors before static teams are replaced",
          "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = std::numeric_limits<std::size_t>::max();
  CHECK_THROWS_AS(sim.Init(OwnershipSetup(50), OwnershipSetup(500),
                           football::pitch::Pitch{}, options),
                  std::length_error);
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim, TeamSide::Home),
                  std::logic_error);
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim, 0), std::logic_error);
  CHECK_FALSE(sim.Stop());
  options.snapshot_capacity = 8;
  REQUIRE_NOTHROW(sim.Init(OwnershipSetup(800), OwnershipSetup(900),
                           football::pitch::Pitch{}, options));
  const auto players = SimulationAccess::TeamOf(sim, 0).Players();
  const auto& team = SimulationAccess::DefinitionOf(sim, TeamSide::Home);
  CHECK(players.front()->GetModel().height == 1.75f);
  CHECK(players.front()->GetID() == 800);
  CHECK(team.players.front().id == 800);
  CHECK(sim.Stop());
}

TEST_CASE("Explicit lineup binds identities to formation slots not declaration indices",
          "[team][ownership]") {
  auto home = OwnershipSetup(50);
  auto bench = home.players.front();
  bench.id = 52;
  home.players.push_back(bench);
  home.lineup.emplace();
  home.lineup->starters[0] = 51;
  home.lineup->starters[8] = 50;
  home.lineup->substitutes = {52};
  home.formation.resize(9);
  home.formation[8] = {{.2f, .3f}, e_PlayerRole_CF};
  home.leadership = {51, {50}};
  home.set_piece_orders.penalties = {52, 50};
  MatchOptions options;
  options.snapshot_capacity = 8;
  Simulation sim;
  sim.Init(home, OwnershipSetup(500), football::pitch::Pitch{}, options);
  const auto players = SimulationAccess::TeamOf(sim, 0).Players();
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
  SimulationAccess::SendOff(sim, *players[1]);
  world = sim.Observe();
  CHECK_FALSE((*world.active_lineups)[0].slots[0]);
  CHECK((*world.active_lineups)[0].slots[8] == 50);
  const auto& team = SimulationAccess::DefinitionOf(sim, TeamSide::Home);
  CHECK(team.lineup.starters[0] == 51);
  const std::vector<football::team::EligiblePlayer> eligible{
      {50, true}, {51, false}, {52, false}};
  CHECK(football::team::SelectCaptain(
            team.leadership, eligible,
            football::team::SelectionFallback::CandidateOrder) == 50);
  CHECK(football::team::SelectEligibleTaker(
            team.set_piece_orders.penalties, eligible,
            football::team::SelectionFallback::None) == 50);
  CHECK(sim.Stop());
}

TEST_CASE("Declared restart configuration leaves the opening ceremony unchanged",
          "[team][ownership]") {
  // The opening/half-time kickoff is a Runtime procedure placed by the legacy
  // ceremony, not a planner restart. Team orders/presets therefore do not
  // change it; ordinary restarts are exercised in the ruling tests.
  const auto home = OwnershipSetup(50);
  const auto away = OwnershipSetup(500);
  auto configured = home;
  configured.leadership = {51, {50}};
  configured.set_piece_orders.penalties = {51, 50};
  configured.formation_presets.presets = {{"kickoff_custom",
      football::team::FormationPresetKind::KickOff,
      football::team::FormationPresetSide::Attacking,
      {{1, {football::team::AnchorFrame::TeamRelative, .4f, .2f}, "receiver"}}}};
  MatchOptions options;
  options.snapshot_capacity = 8;
  Simulation legacy;
  Simulation migrated;
  legacy.Init(home, away, football::pitch::Pitch{}, options);
  migrated.Init(configured, away, football::pitch::Pitch{}, options);
  const auto compare = [&] {
    const auto* a = legacy.Snapshots().Latest();
    const auto* b = migrated.Snapshots().Latest();
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->snapshot.ball.position == b->snapshot.ball.position);
    CHECK(a->snapshot.ball.velocity == b->snapshot.ball.velocity);
    REQUIRE(a->snapshot.players.size() == b->snapshot.players.size());
    for (std::size_t i = 0; i < a->snapshot.players.size(); ++i) {
      CHECK(a->snapshot.players[i].position == b->snapshot.players[i].position);
      CHECK(a->snapshot.players[i].velocity == b->snapshot.players[i].velocity);
      CHECK(a->snapshot.players[i].animation_id == b->snapshot.players[i].animation_id);
      CHECK(a->snapshot.players[i].active == b->snapshot.players[i].active);
    }
    CHECK(SimulationAccess::RngOf(legacy).engine() ==
          SimulationAccess::RngOf(migrated).engine());
    CHECK(legacy.Observe().restart_taker == migrated.Observe().restart_taker);
  };
  compare();
  legacy.Step(PlayerControlSet{});
  migrated.Step(PlayerControlSet{});
  compare();
  CHECK(legacy.Stop());
  CHECK(migrated.Stop());
}

TEST_CASE("Player owns an immutable definition after its source is destroyed",
          "[team][ownership]") {
  AnimationLibrary animations;
  const football::pitch::Pitch pitch;
  football::math::Rng rng;
  const auto before = rng.engine();
  std::unique_ptr<Player> actor;
  {
    football::player::PlayerDefinition source;
    source.id = 4321;
    source.first_name = "Own";
    source.last_name = "Model";
    source.age = 28;
    source.height = 1.9f;
    source.weight_kg = 80.f;
    source.attributes.set(football::player::PlayerStat::mental_vision, .6f);
    source.appearance.skin_color = 3;
    source.appearance.hair_style = "short02";
    source.appearance.hair_color = "black";
    actor = std::make_unique<Player>(0, TeamSide::Home, 1.f, source,
                                     FormationEntry{}, 7, animations, pitch,
                                     rng);
    CHECK(&actor->GetModel() != &source);
    source.id = 0;
    source.height = 0;
    source.first_name.clear();
    source.attributes.fill(0);
  }
  const auto& model = actor->GetModel();
  CHECK(actor->GetID() == 4321);
  CHECK(model.first_name == "Own");
  CHECK(model.last_name == "Model");
  CHECK(model.age == 28);
  CHECK(model.height == 1.9f);
  CHECK(model.weight_kg == 80.f);
  CHECK(model.attributes.get(football::player::PlayerStat::mental_vision) == .6f);
  CHECK(model.appearance.skin_color == 3);
  CHECK(model.appearance.hair_style == "short02");
  CHECK(model.appearance.hair_color == "black");
  CHECK(actor->BodyProfile().height_m == 1.9f);
  CHECK(actor->BodyProfile().mass_kg == 80.f);
  CHECK(rng.engine() == before);
}

TEST_CASE("Runtime owns live lineup, frame and match state separately from Team",
          "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  sim.Init(OwnershipSetup(50), OwnershipSetup(500),
           football::pitch::Pitch{}, options);
  const auto& team = SimulationAccess::DefinitionOf(sim, TeamSide::Home);
  const auto players = SimulationAccess::TeamOf(sim, 0).Players();
  auto& frame = SimulationAccess::TeamFrameOf(sim, 0);
  auto& state = SimulationAccess::TeamStateOf(sim, 0);
  auto& active = SimulationAccess::ActiveLineupOf(sim, 0);
  const auto view = SimulationAccess::TeamViewOf(sim, 0);
  REQUIRE(players.size() == 2);
  CHECK(view.frame == &frame);
  CHECK(view.state == &state);
  CHECK(view.players.data() == players.data());
  CHECK(active.slots == team.lineup.starters);
  const auto cached = players[0]->GetTeamDesignatedPossessionPlayer();
  SimulationAccess::RuntimeOf(sim).SetDesignatedPlayer(0, players[1]);
  CHECK(state.designated_player == players[1]);
  CHECK(players[0]->GetTeamDesignatedPossessionPlayer() == cached);
  SimulationAccess::MirrorTeam(sim, 0);
  CHECK(players[0]->GetTeamDesignatedPossessionPlayer() == players[1]);
  CHECK(frame.dynamic_side == 1);
  SimulationAccess::MirrorTeam(sim, 0);
  CHECK(frame.dynamic_side == -1);
  view.SetFadingPossessionAmount(.8f);
  CHECK(state.fading_possession_amount == .8f);
  CHECK(players[0]->GetTeamFadingPossessionAmount() == .8f);
  SimulationAccess::SwitchTeamEnds(sim, 0);
  CHECK(frame.static_side == 1);
  CHECK(frame.dynamic_side == 1);
  CHECK_FALSE(frame.mirrored);
  CHECK(active.slots == team.lineup.starters);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime Team retains only static configuration", "[team][ownership]") {
  auto home = OwnershipSetup(60);
  home.name = "Initial Only";
  home.tactics = {{"width", 0.7f}};
  home.formation_presets.presets = {{"kickoff",
      football::team::FormationPresetKind::KickOff,
      football::team::FormationPresetSide::Attacking,
      {{0, {football::team::AnchorFrame::TeamRelative, .5f, 0}, "gk"}}}};
  MatchOptions options;
  options.snapshot_capacity = 8;
  Simulation sim;
  sim.Init(home, OwnershipSetup(600), football::pitch::Pitch{}, options);
  const auto& team = SimulationAccess::DefinitionOf(sim, TeamSide::Home);
  CHECK(team.name == "Initial Only");
  CHECK(team.initial_tactics == home.tactics);
  CHECK(team.formations.presets.size() == 1);
  CHECK(team.initial_formation.size() == home.formation.size());
  CHECK(team.tactical_formation.empty());
  CHECK(team.players.size() == home.players.size());
  CHECK(team.players.size() ==
        SimulationAccess::TeamOf(sim, 0).Players().size());
  CHECK(sim.Stop());
}
