// Bounded runtime harness for the simulation composition.
//
// It builds a real Simulation, runs a handful of ticks and tears it down. It is
// deliberately NOT a match simulation: it never drives a match to half time or
// full time and asserts no physics outcome.
//
// Its job is to catch composition and lifecycle errors that the leaf unit tests
// cannot observe at all -- for example a Player holding a dangling
// PlayerDefinition reference (which reads out as height 0 / garbage id). Every
// phase is timed, and the CTest wall-clock timeout is the real guard: bounding
// the tick count cannot catch a single Step that never returns.
//
// Internals (rosters, teams, clock) are reached through the internal
// testing::SimulationAccess escape hatch; this is test-only code and is never
// linked into the product.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_control_set.hpp"
#include "sim/player/player_definition.hpp"
#include "sim/player/player_motion.hpp"
#include "sim/player/player_motion_profile.hpp"
#include "sim/observation/world_state.hpp"
#include "sim/runtime/match_config.hpp"
#include "sim/runtime/possession.hpp"
#include "sim/runtime/roster_possession.hpp"
#include "sim/simulation.hpp"
#include "sim/simulation_config.hpp"
#include "sim/testing/simulation_access.hpp"

namespace {

using Catch::Approx;
using football::sim::testing::SimulationAccess;
using Clock = std::chrono::steady_clock;

double SecondsSince(const Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool IsFinite(const football::math::Vector3& v) {
  return std::isfinite(v.coords[0]) && std::isfinite(v.coords[1]) &&
         std::isfinite(v.coords[2]);
}

// A minimal but complete roster: explicit identities, an 11-strong tactical
// formation, and per-player heights so a dangling PlayerDefinition reads as
// height 0 instead of silently passing.
football::sim::TeamSetup MakeTeam(const char* name,
                                 football::player::PlayerId first_id) {
  football::sim::TeamSetup team;
  team.name = name;
  for (int i = 0; i < 11; ++i) {
    football::player::PlayerDefinition player;
    player.id = first_id + static_cast<football::player::PlayerId>(i);
    player.database_id = 100 + i;
    player.height = 1.70f + 0.01f * static_cast<float>(i);
    team.players.push_back(std::move(player));
  }
  team.tactical_formation = {
      {{-1.0f, 0.0f}, e_PlayerRole_GK},  {{-0.7f, 0.75f}, e_PlayerRole_LB},
      {{-1.0f, 0.25f}, e_PlayerRole_CB}, {{-1.0f, -0.25f}, e_PlayerRole_CB},
      {{-0.7f, -0.75f}, e_PlayerRole_RB}, {{0.0f, 0.5f}, e_PlayerRole_CM},
      {{-0.2f, 0.0f}, e_PlayerRole_CM},  {{0.0f, -0.5f}, e_PlayerRole_CM},
      {{0.6f, 0.75f}, e_PlayerRole_LM},  {{1.0f, 0.0f}, e_PlayerRole_CF},
      {{0.6f, -0.75f}, e_PlayerRole_RM}};
  return team;
}

void InitMatch(Simulation& sim) {
  sim.Init(MakeTeam("home", 0), MakeTeam("away", 11), football::pitch::Pitch{},
           MatchOptions{}, football::ball::BallModel{});
}

int ActiveCountOf(const football::sim::Roster& roster) {
  int count = 0;
  for (Player* player : roster.Players()) {
    if (player->IsActive()) ++count;
  }
  return count;
}

}  // namespace

TEST_CASE("Runtime harness: two rosters initialise with live definitions",
          "[runtime][sim]") {
  Simulation sim;
  const auto init_start = Clock::now();
  InitMatch(sim);
  std::fprintf(stderr, "[runtime] Init: %.3fs\n", SecondsSince(init_start));

  REQUIRE(SimulationAccess::IsInitializedOf(sim));
  const football::sim::Roster& home_roster = SimulationAccess::RosterOf(sim, 0);
  const football::sim::Roster& away_roster = SimulationAccess::RosterOf(sim, 1);




  const std::span<Player* const> home_players = home_roster.Players();
  const std::span<Player* const> away_players = away_roster.Players();
  REQUIRE(home_players.size() == 11);
  REQUIRE(away_players.size() == 11);
  CHECK(ActiveCountOf(home_roster) == 11);
  CHECK(ActiveCountOf(away_roster) == 11);

  for (std::size_t i = 0; i < home_players.size(); ++i) {
    // Roster order, identity and side survive construction.
    CHECK(home_players[i]->GetID() == static_cast<football::player::PlayerId>(i));
    CHECK(away_players[i]->GetID() == static_cast<football::player::PlayerId>(11 + i));
    CHECK(home_players[i]->GetTeamSide() == football::sim::TeamSide::Home);
    CHECK(away_players[i]->GetTeamSide() == football::sim::TeamSide::Away);
    // Player holds a const PlayerDefinition&: a dead reference reads as zero.
    CHECK(home_players[i]->GetModel().height > 1.0f);
    CHECK(home_players[i]->GetModel().id == home_players[i]->GetID());
    CHECK(IsFinite(home_players[i]->GetPosition()));
  }

  const auto stop_start = Clock::now();
  CHECK(sim.Stop());
  std::fprintf(stderr, "[runtime] Stop: %.3fs\n", SecondsSince(stop_start));
  CHECK_FALSE(sim.Stop());  // Idempotent: reports already stopped.
}

TEST_CASE("Runtime harness: initial team orientation is Simulation-owned",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  const football::sim::TeamFrame& home_frame = SimulationAccess::TeamFrameOf(sim, 0);
  const football::sim::TeamFrame& away_frame = SimulationAccess::TeamFrameOf(sim, 1);

  // Historical initial orientation: both teams start in the first processing
  // frame (dynamic -1); only the persistent direction differs by slot. Nothing
  // keeps a copy of this -- Simulation owns the only TeamFrame.
  CHECK(home_frame.dynamic_side == -1);
  CHECK(away_frame.dynamic_side == -1);
  CHECK(home_frame.static_side == -1);
  CHECK(away_frame.static_side == 1);
  CHECK_FALSE(home_frame.mirrored);
  CHECK_FALSE(away_frame.mirrored);
  CHECK(home_frame.dynamic_side == home_frame.static_side);
  CHECK(away_frame.dynamic_side != away_frame.static_side);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a few ticks advance and tear down cleanly",
          "[runtime][sim]") {
  Simulation sim;
  const auto init_start = Clock::now();
  InitMatch(sim);
  std::fprintf(stderr, "[runtime] Init: %.3fs\n", SecondsSince(init_start));

  PlayerControlSet controls;
  const WorldState before = sim.Observe();
  const auto tick_start = Clock::now();
  for (int tick = 0; tick < 5; ++tick) {
    sim.Step(controls);
    std::fprintf(stderr, "[runtime] Step %d: cumulative %.3fs\n", tick + 1,
                 SecondsSince(tick_start));
  }

  const WorldState world = sim.Observe();
  // The timeline must advance, otherwise "it did not crash" would hide a
  // stalled Step loop.
  CHECK(world.tick > before.tick);
  CHECK(world.players.size() == 22);
  CHECK(IsFinite(world.ball_position));
  int active = 0;
  for (const WorldPlayerState& player : world.players) {
    CHECK(player.id != football::player::kInvalidPlayerId);
    CHECK(IsFinite(player.position));
    if (player.active) ++active;
  }
  CHECK(active == 22);

  const auto stop_start = Clock::now();
  CHECK(sim.Stop());
  std::fprintf(stderr, "[runtime] Stop: %.3fs\n", SecondsSince(stop_start));
}

TEST_CASE("Runtime harness: Mirror twice restores players, sides and ball",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  const football::sim::Roster& home_roster = SimulationAccess::RosterOf(sim, 0);
  const football::sim::Roster& away_roster = SimulationAccess::RosterOf(sim, 1);


  const WorldState before = sim.Observe();
  const football::sim::TeamFrame& home_frame = SimulationAccess::TeamFrameOf(sim, 0);
  const int dynamic_before = home_frame.dynamic_side;
  std::vector<int> player_sides;
  for (Player* player : home_roster.Players()) player_sides.push_back(player->GetDynamicSide());

  // Mirror both teams but not the ball; mirroring is its own inverse.
  sim.Mirror(true, true, false);
  CHECK(home_frame.dynamic_side == -dynamic_before);
  for (Player* player : home_roster.Players()) {
    CHECK(player->GetDynamicSide() == -dynamic_before);
  }
  sim.Mirror(true, true, false);

  CHECK(home_frame.dynamic_side == dynamic_before);
  const WorldState after = sim.Observe();
  REQUIRE(after.players.size() == before.players.size());
  for (std::size_t i = 0; i < before.players.size(); ++i) {
    CHECK(after.players[i].id == before.players[i].id);
    CHECK(after.players[i].position.coords[0] == Catch::Approx(before.players[i].position.coords[0]));
    CHECK(after.players[i].position.coords[1] == Catch::Approx(before.players[i].position.coords[1]));
  }
  CHECK(after.ball_position.coords[0] == Catch::Approx(before.ball_position.coords[0]));
  CHECK(after.ball_position.coords[1] == Catch::Approx(before.ball_position.coords[1]));
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: SwitchEnds flips both directions, not the tick frame",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  const football::sim::Roster& home_roster = SimulationAccess::RosterOf(sim, 0);
  const football::sim::Roster& away_roster = SimulationAccess::RosterOf(sim, 1);


  const football::sim::TeamFrame& home_frame = SimulationAccess::TeamFrameOf(sim, 0);
  const int dynamic_before = home_frame.dynamic_side;
  const int static_before = home_frame.static_side;
  const bool mirrored_before = home_frame.mirrored;

  // Half-time change of ends: both directions flip; the canonical between-tick
  // frame (mirrored) must not change.
  SimulationAccess::SwitchTeamEnds(sim, 0);
  CHECK(home_frame.dynamic_side == -dynamic_before);
  CHECK(home_frame.static_side == -static_before);
  CHECK(home_frame.mirrored == mirrored_before);
  // Publication is part of the behaviour: a switched end that skipped it would
  // leave every player reading the previous tick's team facts.
  for (Player* player : home_roster.Players()) {
    CHECK(player->GetDynamicSide() == -dynamic_before);
    CHECK(player->GetStaticSide() == -static_before);
  }

  SimulationAccess::SwitchTeamEnds(sim, 0);
  CHECK(home_frame.dynamic_side == dynamic_before);
  CHECK(home_frame.static_side == static_before);
  CHECK(home_frame.mirrored == mirrored_before);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: possession arbitration is runtime-owned",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  football::sim::Roster& home_roster = SimulationAccess::RosterOf(sim, 0);
  football::sim::TeamMatchState& home_state = SimulationAccess::TeamStateOf(sim, 0);
  football::sim::TeamMatchState& away_state = SimulationAccess::TeamStateOf(sim, 1);

  // The aggregation and the arbitration read the roster and the match state
  // directly; no Team object is involved.
  Player* best = football::sim::BestPossessionPlayer(home_roster);
  REQUIRE(best != nullptr);
  CHECK(best->IsActive());

  home_state.time_to_ball_ms = 50;
  away_state.time_to_ball_ms = 5000;
  const auto selection = football::sim::EvaluatePossession(
      football::sim::TeamSide::Home, home_state, football::sim::TeamSide::Away,
      away_state, best, nullptr);
  REQUIRE(selection.best_team.has_value());
  CHECK(*selection.best_team == football::sim::TeamSide::Home);

  // A retainer is a physical fact and wins over the estimates.
  const auto retained = football::sim::EvaluatePossession(
      football::sim::TeamSide::Home, home_state, football::sim::TeamSide::Away,
      away_state, best, away_state.designated_player);
  REQUIRE(retained.best_team.has_value());
  CHECK(*retained.best_team == football::sim::TeamSide::Away);
  CHECK(retained.designated_player == away_state.designated_player);

  // A send-off re-designation writes the runtime state and publishes it.
  football::sim::RefreshDesignatedTeamPossessionPlayer(
      home_roster, SimulationAccess::TeamFrameOf(sim, 0), home_state,
      *SimulationAccess::BallOf(sim));
  CHECK(home_state.designated_player != nullptr);
  for (Player* player : home_roster.Players()) {
    CHECK(player->GetTeamDesignatedPossessionPlayer() == home_state.designated_player);
  }
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: restart possession weight publishes immediately",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  football::sim::TeamView view = SimulationAccess::TeamViewOf(sim, 0);
  const float before = view.players.front()->GetTeamFadingPossessionAmount();

  // The legacy Team setter clamped the value and published the facts at once.
  // Runtime must keep both halves: a restart placement that only wrote the state
  // would leave every player reading the previous tick's team facts.
  view.SetFadingPossessionAmount(1.5f);
  CHECK(view.fading_possession_amount() == Catch::Approx(1.5f));
  for (Player* player : view.players) {
    CHECK(player->GetTeamFadingPossessionAmount() == Catch::Approx(1.5f));
  }

  // Out-of-range input is clamped exactly as before, and still published.
  view.SetFadingPossessionAmount(9.0f);
  CHECK(view.fading_possession_amount() == Catch::Approx(1.5f));
  view.SetFadingPossessionAmount(0.0f);
  CHECK(view.fading_possession_amount() == Catch::Approx(0.5f));
  for (Player* player : view.players) {
    CHECK(player->GetTeamFadingPossessionAmount() == Catch::Approx(0.5f));
  }
  CHECK(before == Catch::Approx(1.0f));
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: ResetSituation resets team state and actors",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  football::sim::TeamMatchState& state = SimulationAccess::TeamStateOf(sim, 0);

  // Dirty the state first so the reset is observable.
  state.time_to_ball_ms = 4321;
  state.has_possession = true;
  state.possession_amount = 0.25f;
  state.fading_possession_amount = 0.25f;
  state.designated_player = nullptr;

  const football::math::Vector3 focus{5.0f, -3.0f, 0.0f};
  sim.ResetSituation(focus);

  // Legacy order: state first, then the designated player, then the actors with
  // their facts published inside the same loop.
  CHECK(state.time_to_ball_ms == 100);
  CHECK_FALSE(state.has_possession);
  CHECK(state.possession_amount == Catch::Approx(1.0f));
  CHECK(state.fading_possession_amount == Catch::Approx(1.0f));
  REQUIRE(state.designated_player != nullptr);
  CHECK(state.designated_player->IsActive());
  for (Player* player : SimulationAccess::RosterOf(sim, 0).Players()) {
    CHECK(IsFinite(player->GetPosition()));
    // Published projection matches the reset state.
    CHECK(player->GetTeamFadingPossessionAmount() == Catch::Approx(1.0f));
  }
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: Stop then re-Init rebuilds both rosters",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  for (int i = 0; i < 2; ++i) sim.Step(PlayerControlSet{});
  CHECK(sim.Stop());

  // A second Init must not inherit players, state or dangling references.
  InitMatch(sim);
  for (int team = 0; team < 2; ++team) {
    const football::sim::Roster& roster = SimulationAccess::RosterOf(sim, team);
    REQUIRE(roster.Players().size() == 11);
    for (std::size_t i = 0; i < roster.Players().size(); ++i) {
      Player* player = roster.Players()[i];
      CHECK(player->GetID() ==
            static_cast<football::player::PlayerId>(team * 11 + i));
      CHECK(player->GetTeamSide() == (team == 0 ? football::sim::TeamSide::Home
                                                : football::sim::TeamSide::Away));
      // The PlayerDefinition reference must point at the new team_setups_.
      CHECK(player->GetModel().height > 1.0f);
      CHECK(player->GetModel().id == player->GetID());
    }
    CHECK(SimulationAccess::TeamStateOf(sim, team).designated_player != nullptr);
  }
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: motion profile is a faithful static extraction",
          "[runtime][sim]") {
  football::player::PlayerDefinition neutral;  // every attribute defaults to 1.0
  const PlayerMotionProfile neutral_profile = MakePlayerMotionProfile(neutral);
  const PlayerLocomotionParameters defaults;

  // A neutral definition must reproduce the locomotion defaults exactly; the
  // profile is an extraction, not a new physics model.
  CHECK(neutral_profile.max_speed == Approx(7.0f));       // animSprintVelocity * 1.0
  CHECK(neutral_profile.acceleration == Approx(defaults.acceleration));
  CHECK(neutral_profile.braking == Approx(defaults.braking));
  CHECK(neutral_profile.turn_rate == Approx(defaults.maxTurnRate));

  football::player::PlayerDefinition athletic = neutral;
  athletic.attributes.set(football::player::PlayerStat::physical_velocity, 2.0f);
  athletic.attributes.set(football::player::PlayerStat::physical_acceleration, 2.0f);
  athletic.attributes.set(football::player::PlayerStat::physical_agility, 2.0f);
  const PlayerMotionProfile athletic_profile = MakePlayerMotionProfile(athletic);
  CHECK(athletic_profile.max_speed > neutral_profile.max_speed);
  CHECK(athletic_profile.acceleration > neutral_profile.acceleration);
  CHECK(athletic_profile.turn_rate > neutral_profile.turn_rate);
  // Pure function of the definition: no RNG, no hidden state, no fatigue baked in.
  CHECK(MakePlayerMotionProfile(athletic).max_speed == Approx(athletic_profile.max_speed));
  CHECK(MakePlayerMotionProfile(neutral).braking == Approx(neutral_profile.braking));
}

TEST_CASE("Runtime harness: PlayerMotion proposes, Player commits",
          "[runtime][sim]") {
  const PlayerMotionProfile profile =
      MakePlayerMotionProfile(football::player::PlayerDefinition{});
  PlayerKinematicState state;
  state.position = Vector3(0, 0, 0);
  state.velocity = Vector3(0, 0, 0);
  state.facing = Vector3(0, -1, 0);

  const MotionSample continuous = PlayerMotion::SampleContinuous(
      state, profile, Vector3(5, 0, 0), Vector3(0, -1, 0), 0.01f);
  CHECK(continuous.velocity.coords[0] > 0.0f);
  CHECK(continuous.displacement.coords[2] == Approx(0.0f));
  // A sample is a proposal: the caller's state is untouched.
  CHECK(state.position == Vector3(0, 0, 0));
  CHECK(state.velocity == Vector3(0, 0, 0));

  // The baked source is planar and needs no clip mutation.
  MotionClip clip;
  clip.frames.resize(4);
  for (std::size_t i = 1; i < clip.frames.size(); ++i) {
    clip.frames[i].local_displacement = Vector3(0.07f, 0, 0);
  }
  const MotionSample baked = PlayerMotion::SampleClip(clip, 2.0f, 0.01f);
  CHECK(baked.displacement.coords[0] == Approx(0.07f));
  CHECK(baked.displacement.coords[2] == Approx(0.0f));
  CHECK(baked.velocity.coords[0] == Approx(7.0f).margin(1e-4f));
  CHECK(PlayerMotion::ClipDisplacement(clip, 0.0f).GetLength() == Approx(0.0f));
  CHECK(PlayerMotion::ClipDisplacement(clip, 2.5f).coords[0] == Approx(0.07f));
}
