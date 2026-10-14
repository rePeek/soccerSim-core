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
#include "sim/player/humanoid/humanoid.hpp"
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

TEST_CASE("Runtime harness: P2a motion step is bit-identical to the legacy call",
          "[runtime][sim]") {
  // A profile that pins acceleration/braking/turn_rate to the locomotion
  // defaults, i.e. the P2a cut. Since P2b-1 CommitLocomotion no longer does
  // that, this case verifies the *facade* (StepContinuous must be exactly
  // PlayerLocomotion::Step), not the production profile.
  const football::player::PlayerDefinition definition;
  const PlayerLocomotionParameters defaults;
  const float max_speed =
      7.0f * (0.9f + 0.1f * definition.attributes.get(
                                 football::player::PlayerStat::physical_velocity));
  PlayerMotionProfile profile = MakePlayerMotionProfile(definition);
  profile.max_speed = max_speed;
  profile.acceleration = defaults.acceleration;
  profile.braking = defaults.braking;
  profile.turn_rate = defaults.maxTurnRate;

  struct Case {
    Vector3 velocity;
    Vector3 facing;
    Vector3 desired;
    Vector3 idle;
  };
  const Case cases[] = {
      {{0, 0, 0}, {0, -1, 0}, {5, 0, 0}, {0, -1, 0}},   // accelerate
      {{5, 0, 0}, {1, 0, 0}, {0, 5, 0}, {1, 0, 0}},     // turn
      {{3, 3, 0}, {0, 1, 0}, {-3, -3, 0}, {0, 1, 0}},   // reverse
      {{7, 0, 0}, {1, 0, 0}, {0, 0, 0}, {0, -1, 0}},    // brake
      {{0.4f, 0, 0}, {1, 0, 0}, {0, 0, 0}, {1, 0, 0}},  // below idle threshold
  };

  for (const Case& c : cases) {
    PlayerKinematicState start;
    start.position = Vector3(1.0f, 2.0f, 0.0f);
    start.velocity = c.velocity;

    PlayerLocomotionInput input;
    input.desiredVelocity = c.desired;
    input.idleFacing = c.idle;
    PlayerLocomotionParameters parameters;
    parameters.maxSpeed = max_speed;
    PlayerKinematicState expected = start;
    PlayerLocomotion::Step(expected, input, parameters, 0.01f);

    const MotionStep step =
        PlayerMotion::StepContinuous(start, profile, c.desired, c.idle, 0.01f);
    // The facade must not change the arithmetic: same function, same parameters.
    CHECK(step.state.position.coords[0] == expected.position.coords[0]);
    CHECK(step.state.position.coords[1] == expected.position.coords[1]);
    CHECK(step.state.velocity.coords[0] == expected.velocity.coords[0]);
    CHECK(step.state.velocity.coords[1] == expected.velocity.coords[1]);
    CHECK(step.state.GetFacing().coords[0] == expected.GetFacing().coords[0]);
    CHECK(step.state.GetFacing().coords[1] == expected.GetFacing().coords[1]);
    CHECK(step.state.GetSpeed() == expected.GetSpeed());
    // The proposal and the committed state cannot disagree.
    CHECK(step.sample.velocity.coords[0] == step.state.velocity.coords[0]);
    CHECK(step.sample.displacement.coords[0] ==
          step.state.position.coords[0] - start.position.coords[0]);
    const MotionSample sample =
        PlayerMotion::SampleContinuous(start, profile, c.desired, c.idle, 0.01f);
    CHECK(sample.velocity.coords[1] == step.state.velocity.coords[1]);
    // Stepping never mutates the caller's state.
    CHECK(start.position.coords[0] == 1.0f);
    CHECK(start.velocity.coords[0] == c.velocity.coords[0]);
  }
}

TEST_CASE("Runtime harness: capability profile changes acceleration and turning",
          "[runtime][sim]") {
  constexpr float kDt = 0.01f;
  football::player::PlayerDefinition neutral;
  football::player::PlayerDefinition athletic = neutral;
  athletic.attributes.set(football::player::PlayerStat::physical_acceleration, 2.0f);
  athletic.attributes.set(football::player::PlayerStat::physical_agility, 2.0f);
  const PlayerMotionProfile neutral_profile = MakePlayerMotionProfile(neutral);
  const PlayerMotionProfile athletic_profile = MakePlayerMotionProfile(athletic);
  REQUIRE(athletic_profile.acceleration > neutral_profile.acceleration);
  REQUIRE(athletic_profile.braking > neutral_profile.braking);
  REQUIRE(athletic_profile.turn_rate > neutral_profile.turn_rate);
  // The ability mapping never invents a top speed of its own.
  CHECK(neutral_profile.max_speed == Approx(athletic_profile.max_speed));

  PlayerKinematicState start;
  start.position = Vector3(0, 0, 0);
  const Vector3 sprint(7.0f, 0.0f, 0.0f);

  // Acceleration: same input, the athletic player covers more ground.
  const PlayerKinematicState neutral_accel =
      PlayerMotion::StepContinuous(start, neutral_profile, sprint, start.GetFacing(), kDt).state;
  const PlayerKinematicState athletic_accel =
      PlayerMotion::StepContinuous(start, athletic_profile, sprint, start.GetFacing(), kDt).state;
  CHECK(athletic_accel.GetSpeed() > neutral_accel.GetSpeed());
  CHECK(athletic_accel.position.GetDistance(start.position) >
        neutral_accel.position.GetDistance(start.position));

  // Braking: from a moving start toward a standstill.
  PlayerKinematicState moving = start;
  moving.velocity = Vector3(5.0f, 0.0f, 0.0f);
  const PlayerKinematicState neutral_stop =
      PlayerMotion::StepContinuous(moving, neutral_profile, Vector3(0), moving.GetFacing(), kDt).state;
  const PlayerKinematicState athletic_stop =
      PlayerMotion::StepContinuous(moving, athletic_profile, Vector3(0), moving.GetFacing(), kDt).state;
  CHECK(athletic_stop.GetSpeed() < neutral_stop.GetSpeed());

  // Turning: same requested heading change at speed, the agile player ends closer
  // to it, and neither exceeds the per-tick turn rate bound.
  const Vector3 want(0.0f, 5.0f, 0.0f);
  const PlayerKinematicState neutral_turn =
      PlayerMotion::StepContinuous(moving, neutral_profile, want, moving.GetFacing(), kDt).state;
  const PlayerKinematicState athletic_turn =
      PlayerMotion::StepContinuous(moving, athletic_profile, want, moving.GetFacing(), kDt).state;
  const float neutral_error = std::fabs(neutral_turn.GetFacing().GetAngle2D(want));
  const float athletic_error = std::fabs(athletic_turn.GetFacing().GetAngle2D(want));
  CHECK(athletic_error <= neutral_error);
  CHECK(std::fabs(neutral_turn.GetFacing().GetAngle2D(moving.GetFacing())) <=
        neutral_profile.turn_rate * kDt + 1e-4f);
  CHECK(std::fabs(athletic_turn.GetFacing().GetAngle2D(moving.GetFacing())) <=
        athletic_profile.turn_rate * kDt + 1e-4f);
  // Turning costs speed in both, and nothing leaves the finite range.
  CHECK(neutral_turn.GetSpeed() < moving.GetSpeed());
  CHECK(athletic_turn.GetSpeed() < moving.GetSpeed());
  CHECK(athletic_turn.GetSpeed() <= athletic_profile.max_speed);
  for (const PlayerKinematicState* state :
       {&neutral_accel, &athletic_accel, &neutral_stop, &athletic_stop,
        &neutral_turn, &athletic_turn}) {
    CHECK(std::isfinite(state->GetSpeed()));
    CHECK(IsFinite(state->position));
    CHECK(IsFinite(state->velocity));
    CHECK(IsFinite(state->GetFacing()));
    CHECK(state->GetSpeed() <= athletic_profile.max_speed + 1e-4f);
  }
}

TEST_CASE("Runtime harness: capability motion replays deterministically",
          "[runtime][sim]") {
  constexpr float kDt = 0.01f;
  football::player::PlayerDefinition definition;
  definition.attributes.set(football::player::PlayerStat::physical_acceleration, 1.7f);
  definition.attributes.set(football::player::PlayerStat::physical_agility, 0.6f);
  definition.attributes.set(football::player::PlayerStat::physical_velocity, 1.3f);
  const PlayerMotionProfile profile = MakePlayerMotionProfile(definition);

  // A deterministic control sequence: no RNG anywhere on the motion path, so the
  // same sequence must reproduce the same trajectory exactly.
  const auto run = [&profile](int ticks) {
    std::vector<PlayerKinematicState> states;
    states.reserve(static_cast<std::size_t>(ticks) + 1);
    PlayerKinematicState state;
    state.position = Vector3(10.0f, -4.0f, 0.0f);
    states.push_back(state);
    for (int tick = 0; tick < ticks; ++tick) {
      const float phase = static_cast<float>(tick) * 0.05f;
      const Vector3 desired(7.0f * std::cos(phase), 7.0f * std::sin(phase), 0.0f);
      state = PlayerMotion::StepContinuous(state, profile, desired, state.GetFacing(), kDt).state;
      states.push_back(state);
    }
    return states;
  };

  const std::vector<PlayerKinematicState> first = run(200);
  const std::vector<PlayerKinematicState> repeat = run(200);
  REQUIRE(first.size() == repeat.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    // Bit-exact, not approximate: determinism is the contract.
    CHECK(first[i].position.coords[0] == repeat[i].position.coords[0]);
    CHECK(first[i].position.coords[1] == repeat[i].position.coords[1]);
    CHECK(first[i].velocity.coords[0] == repeat[i].velocity.coords[0]);
    CHECK(first[i].velocity.coords[1] == repeat[i].velocity.coords[1]);
    CHECK(first[i].GetFacing().coords[0] == repeat[i].GetFacing().coords[0]);
    CHECK(first[i].GetSpeed() == repeat[i].GetSpeed());
  }

  // Replaying from the initial state after a different run is still identical: no
  // hidden accumulators survive a reset.
  run(37);
  const std::vector<PlayerKinematicState> after_interference = run(200);
  for (std::size_t i = 0; i < first.size(); ++i) {
    CHECK(first[i].position.coords[0] == after_interference[i].position.coords[0]);
    CHECK(first[i].velocity.coords[1] == after_interference[i].velocity.coords[1]);
    CHECK(first[i].GetFacing().coords[1] == after_interference[i].GetFacing().coords[1]);
  }

  // The trajectory is bounded and finite over the whole run.
  for (const PlayerKinematicState& state : first) {
    CHECK(IsFinite(state.position));
    CHECK(IsFinite(state.velocity));
    CHECK(state.GetSpeed() <= profile.max_speed + 1e-4f);
  }
}

TEST_CASE("Runtime harness: state commit unifies locomotion and legacy sync",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players().front();
  REQUIRE(player != nullptr);
  Humanoid* humanoid = player->CastHumanoid();
  REQUIRE(humanoid != nullptr);

  // 1) A real tick runs the production chain: Player::CommitLocomotion ->
  // HumanoidBase::ApplySimulationMovementState -> SynchronizeKinematicState.
  // Player and its legacy mirror must agree afterwards.
  sim.Step(PlayerControlSet{});
  const PlayerKinematicState after_tick = player->GetKinematicState();
  CHECK(player->GetPosition() == after_tick.position);
  CHECK(player->GetMovement() == after_tick.velocity);
  CHECK(humanoid->GetPosition() == after_tick.position);
  CHECK(humanoid->GetMovement() == after_tick.velocity);
  CHECK(humanoid->GetDirectionVec() == after_tick.GetFacing());
  CHECK(humanoid->GetBodyDirectionVec() == after_tick.bodyFacing);

  // 2) Contact corrections are committed by the Player first (P2c-2). They
  // accumulate, touch nothing but the position, and the legacy Humanoid
  // bookkeeping adopts the same offset so both sides stay consistent.
  const Vector3 offset_a(0.10f, 0.0f, 0.0f);
  const Vector3 offset_b(0.0f, -0.05f, 0.0f);
  player->OffsetPosition(offset_a);
  const Vector3 after_a = player->GetPosition();
  // Visible immediately on the Player, and exactly the position that moved.
  CHECK(after_a == after_tick.position + offset_a);
  CHECK(player->GetKinematicState().velocity == after_tick.velocity);
  CHECK(player->GetKinematicState().bodyFacing == after_tick.bodyFacing);
  CHECK(player->GetKinematicState().GetSpeed() == after_tick.GetSpeed());
  CHECK(humanoid->GetPosition() == after_a);
  CHECK(player->GetGroundCollider().center == after_a.Get2D());

  player->OffsetPosition(offset_b);
  const Vector3 after_b = player->GetPosition();
  CHECK(after_b == after_a + offset_b);  // corrections accumulate exactly
  CHECK(humanoid->GetPosition() == after_b);
  CHECK(player->GetKinematicState().velocity == after_tick.velocity);

  // 3) The corrected state is the origin of the next motion computation: a later
  // step must not jump back to where the player stood before the contact.
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Movement;
  command.useDesiredMovement = true;
  command.desiredDirection = Vector3(1.0f, 0.0f, 0.0f);
  command.desiredVelocityFloat = 5.0f;
  const PlayerKinematicState after_contact = player->GetKinematicState();
  const PlayerKinematicState next_step =
      player->CommitLocomotion(command, after_contact, false, 0.01f);
  CHECK(next_step.position.GetDistance(after_contact.position) < 0.01f);
  CHECK(next_step.position.GetDistance(after_tick.position) > 0.05f);

  // 4) Mirror round trip with a correction in between stays consistent on both
  // sides and returns the frame to its canonical orientation.
  sim.Step(PlayerControlSet{});
  const Vector3 before_mirror = player->GetPosition();
  sim.Mirror(true, true, false);
  player->OffsetPosition(offset_a);
  CHECK(humanoid->GetPosition() == player->GetPosition());
  sim.Mirror(true, true, false);
  CHECK(humanoid->GetPosition() == player->GetPosition());
  CHECK(player->GetKinematicState().position == player->GetPosition());
  CHECK(player->GetPosition().GetDistance(before_mirror) > 0.0f);

  // 5) A reset clears the correction and both sides stay consistent.
  sim.ResetSituation(Vector3(0.0f, 0.0f, 0.0f));
  sim.Step(PlayerControlSet{});
  CHECK(humanoid->GetPosition() == player->GetPosition());
  CHECK(player->GetKinematicState().position == player->GetPosition());
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: the Player is the locomotion authority across ticks",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  PlayerControlSet controls;

  const int procedural_before = HumanoidProceduralMovementTicks();

  for (int tick = 0; tick < 200; ++tick) {
    sim.Step(controls);
    // Whichever path ran this tick, the Player's committed state and the legacy
    // Humanoid projection must agree. On the pure-locomotion path the Player
    // writes first and the projection must not adopt anything back, so this
    // invariant is what proves there is no hidden second authority.
    for (int team = 0; team < 2; ++team) {
      for (Player* player : SimulationAccess::RosterOf(sim, team).Players()) {
        Humanoid* humanoid = player->CastHumanoid();
        if (humanoid == nullptr) continue;
        REQUIRE(humanoid->GetPosition() == player->GetKinematicState().position);
        REQUIRE(humanoid->GetMovement() == player->GetKinematicState().velocity);
      }
    }
  }
  const int procedural_ticks =
      HumanoidProceduralMovementTicks() - procedural_before;
  std::fprintf(stderr, "[runtime] procedural locomotion ticks=%d\n",
               procedural_ticks);
  // The pure-locomotion path must actually have run, otherwise this case would
  // only be proving that the legacy path stays consistent.
  CHECK(procedural_ticks > 0);

  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: lifecycle paths keep one Player authority",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players().front();
  REQUIRE(player != nullptr);
  Humanoid* humanoid = player->CastHumanoid();
  REQUIRE(humanoid != nullptr);
  sim.Step(PlayerControlSet{});

  const PlayerKinematicState base = player->GetKinematicState();

  // Mirror commits through the Player and keeps the historical asymmetry:
  // position and velocity are mirrored, bodyFacing is not. The derived
  // GetFacing() therefore mirrors through velocity while moving, and at rest
  // stays on the un-mirrored bodyFacing (verified by the round trip below).
  sim.Mirror(true, true, false);
  const PlayerKinematicState mirrored = player->GetKinematicState();
  CHECK(mirrored.position == -base.position);
  CHECK(mirrored.velocity == -base.velocity);
  CHECK(mirrored.bodyFacing == base.bodyFacing);
  CHECK(humanoid->GetPosition() == mirrored.position);
  CHECK(humanoid->GetMovement() == mirrored.velocity);
  CHECK(player->GetGroundCollider().center == mirrored.position.Get2D());

  // Mirroring is its own inverse, so a round trip restores every field.
  sim.Mirror(true, true, false);
  CHECK(player->GetKinematicState().position == base.position);
  CHECK(player->GetKinematicState().velocity == base.velocity);
  CHECK(player->GetKinematicState().bodyFacing == base.bodyFacing);
  CHECK(humanoid->GetPosition() == base.position);

  // A contact correction followed by a reset must not leave residue, and both
  // sides must agree after the lifecycle entry.
  player->OffsetPosition(Vector3(0.2f, 0.1f, 0.0f));
  player->ResetPosition(Vector3(3.0f, -2.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
  CHECK(player->GetPosition().coords[0] == Approx(3.0f));
  CHECK(player->GetPosition().coords[1] == Approx(-2.0f));
  CHECK(player->GetKinematicState().position == player->GetPosition());
  CHECK(humanoid->GetPosition() == player->GetPosition());

  // The situation reset path (which also adopts the Humanoid pose) stays
  // consistent across a following tick.
  sim.ResetSituation(Vector3(1.0f, 1.0f, 0.0f));
  sim.Step(PlayerControlSet{});
  CHECK(humanoid->GetPosition() == player->GetPosition());
  CHECK(player->GetKinematicState().position == player->GetPosition());
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: one effective motion profile, static-driven",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players().front();
  REQUIRE(player != nullptr);

  // S1.1: execution and prediction must read the same capability source, and that
  // source must be the definition-derived profile with the established top-speed
  // formula.
  const PlayerMotionProfile profile = player->EffectiveMotionProfile();
  const PlayerMotionProfile from_definition =
      MakePlayerMotionProfile(player->GetModel());
  CHECK(profile.acceleration == Approx(from_definition.acceleration));
  CHECK(profile.braking == Approx(from_definition.braking));
  CHECK(profile.turn_rate == Approx(from_definition.turn_rate));
  CHECK(profile.max_speed == Approx(player->GetMaxVelocity()));

  // A neutral definition must still reproduce the locomotion defaults, so the
  // profile stays an extraction rather than a new physics model.
  football::player::PlayerDefinition neutral;
  const PlayerMotionProfile neutral_profile = MakePlayerMotionProfile(neutral);
  const PlayerLocomotionParameters defaults;
  CHECK(neutral_profile.acceleration == Approx(defaults.acceleration));
  CHECK(neutral_profile.braking == Approx(defaults.braking));
  CHECK(neutral_profile.turn_rate == Approx(defaults.maxTurnRate));
  CHECK(neutral_profile.max_speed == Approx(7.0f));

  // Pinned decision: fatigue and AI difficulty scale abilities today, but they do
  // NOT yet scale the motion profile. Whether they should is a separate behaviour
  // change; if it happens, this expectation is the one that must change.
  const float fatigue_before = player->GetFatigueFactorInv();
  player->RelaxFatigue(1.0f);
  CHECK(player->GetFatigueFactorInv() >= fatigue_before);
  const PlayerMotionProfile after_fatigue = player->EffectiveMotionProfile();
  CHECK(after_fatigue.max_speed == profile.max_speed);
  CHECK(after_fatigue.acceleration == profile.acceleration);
  CHECK(after_fatigue.braking == profile.braking);
  CHECK(after_fatigue.turn_rate == profile.turn_rate);

  // Determinism: the profile is a pure function of the definition plus the
  // explicit runtime state, so reading it twice cannot differ.
  const PlayerMotionProfile again = player->EffectiveMotionProfile();
  CHECK(again.max_speed == after_fatigue.max_speed);
  CHECK(again.acceleration == after_fatigue.acceleration);
  CHECK(again.braking == after_fatigue.braking);
  CHECK(again.turn_rate == after_fatigue.turn_rate);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: animation adaptation never changes the committed trajectory",
          "[runtime][sim]") {
  // LOCO-2.3b controlled A/B: the motion-driven Movement reorder may pick a
  // different clip, but the Player position/velocity are committed by
  // PlayerMotion before SelectAnim runs, so they must be bit-identical.
  const auto run = [](bool adaptation) {
    Simulation sim;
    InitMatch(sim);
    SimulationAccess::EnableLocomotionAnimationAdaptation(sim, adaptation);
    PlayerControlSet controls;
    for (int tick = 0; tick < 50; ++tick) sim.Step(controls);
    std::vector<PlayerKinematicState> states;
    for (int team = 0; team < 2; ++team)
      for (Player* player : SimulationAccess::RosterOf(sim, team).Players())
        states.push_back(player->GetKinematicState());
    sim.Stop();
    return states;
  };

  const std::vector<PlayerKinematicState> on = run(true);
  const std::vector<PlayerKinematicState> off = run(false);
  REQUIRE(on.size() == off.size());
  REQUIRE(on.size() == 22);
  for (std::size_t i = 0; i < on.size(); ++i) {
    CHECK(on[i].position.coords[0] == off[i].position.coords[0]);
    CHECK(on[i].position.coords[1] == off[i].position.coords[1]);
    CHECK(on[i].velocity.coords[0] == off[i].velocity.coords[0]);
    CHECK(on[i].velocity.coords[1] == off[i].velocity.coords[1]);
    CHECK(on[i].GetSpeed() == off[i].GetSpeed());
  }
}

TEST_CASE("Runtime harness: movement pose blend never changes the committed trajectory",
          "[runtime][sim]") {
  // LOCO-2.6c/d: the pose blend between Movement clips changes only the final
  // pose (body CCD), never the Player position/velocity committed by
  // PlayerMotion.
  const auto run = [](bool blend) {
    Simulation sim;
    InitMatch(sim);
    SimulationAccess::EnableMovementPoseBlend(sim, blend);
    PlayerControlSet controls;
    for (int tick = 0; tick < 50; ++tick) sim.Step(controls);
    std::vector<PlayerKinematicState> states;
    for (int team = 0; team < 2; ++team)
      for (Player* player : SimulationAccess::RosterOf(sim, team).Players())
        states.push_back(player->GetKinematicState());
    sim.Stop();
    return states;
  };

  const std::vector<PlayerKinematicState> on = run(true);
  const std::vector<PlayerKinematicState> off = run(false);
  REQUIRE(on.size() == off.size());
  REQUIRE(on.size() == 22);
  for (std::size_t i = 0; i < on.size(); ++i) {
    CHECK(on[i].position.coords[0] == off[i].position.coords[0]);
    CHECK(on[i].position.coords[1] == off[i].position.coords[1]);
    CHECK(on[i].velocity.coords[0] == off[i].velocity.coords[0]);
    CHECK(on[i].velocity.coords[1] == off[i].velocity.coords[1]);
    CHECK(on[i].GetSpeed() == off[i].GetSpeed());
  }
}

TEST_CASE("Runtime harness: locomotion playback reference is recorded, not applied",
          "[runtime][sim]") {
  // LOCO-2.3c: the strategy computes and records a reference playback rate,
  // but the integer frameNum clock is not advanced by it until LOCO-2.6.
  const int before = LocomotionPlaybackAuditState().samples;
  Simulation sim;
  InitMatch(sim);
  PlayerControlSet controls;
  for (int tick = 0; tick < 30; ++tick) sim.Step(controls);
  const LocomotionPlaybackAudit& audit = LocomotionPlaybackAuditState();
  CHECK(audit.samples > before);
  CHECK(std::isfinite(audit.reference_speed_sum));
  CHECK(std::isfinite(audit.playback_rate_sum));
  CHECK(audit.playback_rate_max >= 0.0f);
  CHECK(sim.Stop());
}
