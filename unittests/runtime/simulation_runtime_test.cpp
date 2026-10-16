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
#include <variant>
#include <limits>
#include <cstdint>
#include <cstring>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_diagnostics.hpp"
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

using football::sim::testing::SimulationAccess;
using Clock = std::chrono::steady_clock;

double SecondsSince(const Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool IsFinite(const football::math::Vector3& v) {
  return std::isfinite(v.coords[0]) && std::isfinite(v.coords[1]) &&
         std::isfinite(v.coords[2]);
}

// ANIM-2 A15: pose/collider continuity helpers over consecutive ticks.
float MaxJointJump(const PoseFrame& a, const PoseFrame& b) {
  float jump = 0.0f;
  for (std::size_t j = 0; j < kBodyPartCount; ++j) {
    // Align the neighbourhood first: a sign-flipped but identical rotation is
    // not a continuity break.
    auto aligned = b.orientations[j];
    aligned.MakeSameNeighborhood(a.orientations[j]);
    jump = std::max(
        jump, static_cast<float>(a.orientations[j].GetRotationAngle(aligned)));
  }
  return jump;
}

football::math::Vector3 BodyShapeCenter(const football::geometry::ColliderShape& shape) {
  if (const auto* capsule = std::get_if<football::geometry::Capsule>(&shape))
    return (capsule->a + capsule->b) * 0.5f;
  if (const auto* sphere = std::get_if<football::geometry::Sphere>(&shape))
    return sphere->center;
  return football::math::Vector3(0);
}

float MaxColliderCenterDelta(const BakedPlayerBodyCollider& a,
                             const BakedPlayerBodyCollider& b,
                             const football::math::Vector3& body_delta) {
  // Subtract the whole-body displacement (collision corrections move every
  // shape equally); what remains is the pose-driven collider change.
  float delta = 0.0f;
  for (std::size_t i = 0; i < a.shapes.size(); ++i) {
    const football::math::Vector3 relative =
        (BodyShapeCenter(b.shapes[i]) - BodyShapeCenter(a.shapes[i])) - body_delta;
    delta = std::max(delta, relative.GetLength());
  }
  return delta;
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

// A deliberately tiny roster for tests that must reach play but only need a
// single tracked actor. The ceremony length is unchanged; only the per-tick
// cost drops, which keeps the bounded runtime suite inside its timeout.
football::sim::TeamSetup MakeSmallTeam(const char* name,
                                        football::player::PlayerId first_id) {
  football::sim::TeamSetup team;
  team.name = name;
  for (int i = 0; i < 2; ++i) {
    football::player::PlayerDefinition player;
    player.id = first_id + static_cast<football::player::PlayerId>(i);
    player.database_id = 100 + i;
    player.height = 1.75f;
    team.players.push_back(std::move(player));
  }
  team.tactical_formation = {
      {{-1.0f, 0.0f}, e_PlayerRole_GK},
      {{0.4f, 0.0f}, e_PlayerRole_CF}};
  return team;
}

void InitSmallMatch(Simulation& sim) {
  sim.Init(MakeSmallTeam("home", 0), MakeSmallTeam("away", 2),
           football::pitch::Pitch{}, MatchOptions{}, football::ball::BallModel{});
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

  // 1) A real tick runs the production chain: Player::CommitLocomotion ->
  // the compatibility projection -> kinematic commit. The Player's public
  // movement readers must all agree with the committed state afterwards.
  sim.Step(PlayerControlSet{});
  const PlayerKinematicState after_tick = player->GetKinematicState();
  CHECK(player->GetPosition() == after_tick.position);
  CHECK(player->GetMovement() == after_tick.velocity);

  // 2) Contact corrections are committed by the Player first (P2c-2). They
  // accumulate and touch nothing but the position.
  const Vector3 offset_a(0.10f, 0.0f, 0.0f);
  const Vector3 offset_b(0.0f, -0.05f, 0.0f);
  player->OffsetPosition(offset_a);
  const Vector3 after_a = player->GetPosition();
  // Visible immediately on the Player, and exactly the position that moved.
  CHECK(after_a == after_tick.position + offset_a);
  CHECK(player->GetKinematicState().velocity == after_tick.velocity);
  CHECK(player->GetKinematicState().bodyFacing == after_tick.bodyFacing);
  CHECK(player->GetKinematicState().GetSpeed() == after_tick.GetSpeed());
  CHECK(player->GetGroundCollider().center == after_a.Get2D());

  player->OffsetPosition(offset_b);
  const Vector3 after_b = player->GetPosition();
  CHECK(after_b == after_a + offset_b);  // corrections accumulate exactly
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

  // 4) Mirror round trip with a correction in between stays consistent and
  // returns the frame to its canonical orientation.
  sim.Step(PlayerControlSet{});
  const Vector3 before_mirror = player->GetPosition();
  sim.Mirror(true, true, false);
  player->OffsetPosition(offset_a);
  sim.Mirror(true, true, false);
  CHECK(player->GetKinematicState().position == player->GetPosition());
  CHECK(player->GetPosition().GetDistance(before_mirror) > 0.0f);

  // 5) A reset clears the correction and the Player stays consistent.
  sim.ResetSituation(Vector3(0.0f, 0.0f, 0.0f));
  sim.Step(PlayerControlSet{});
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
    // The Player is the single locomotion authority: the pure-locomotion path
    // commits directly and no compatibility projection can adopt anything back.
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
  CHECK(player->GetGroundCollider().center == mirrored.position.Get2D());

  // Mirroring is its own inverse, so a round trip restores every field.
  sim.Mirror(true, true, false);
  CHECK(player->GetKinematicState().position == base.position);
  CHECK(player->GetKinematicState().velocity == base.velocity);
  CHECK(player->GetKinematicState().bodyFacing == base.bodyFacing);

  // A contact correction followed by a reset must not leave residue, and both
  // sides must agree after the lifecycle entry.
  player->OffsetPosition(Vector3(0.2f, 0.1f, 0.0f));
  player->ResetPosition(Vector3(3.0f, -2.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
  CHECK(player->GetPosition().coords[0] == Approx(3.0f));
  CHECK(player->GetPosition().coords[1] == Approx(-2.0f));
  CHECK(player->GetKinematicState().position == player->GetPosition());

  // The situation reset path stays consistent across a following tick.
  sim.ResetSituation(Vector3(1.0f, 1.0f, 0.0f));
  sim.Step(PlayerControlSet{});
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


TEST_CASE("Runtime harness: reset makes body facing consistent with the focus",
          "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[9];

  // Non-default focus: face +Y relative to the new position.
  const Vector3 new_pos(10.0f, 0.0f, 0.0f);
  const Vector3 focus(10.0f, 5.0f, 0.0f);
  player->ResetPosition(new_pos, focus);

  const PlayerKinematicState state = player->GetKinematicState();
  // Body facing, locomotion direction and the animation base now agree after a
  // reset, and reflect the focus instead of the old hard-coded world -Y.
  // The Player is the only authority: body facing and the derived locomotion
  // facing agree and both reflect the focus, not a hard-coded world -Y.
  CHECK(state.bodyFacing == state.GetFacing());
  CHECK(state.bodyFacing != Vector3(0.0f, -1.0f, 0.0f));
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: per-player Play/React/Pose contract", "[runtime][sim]") {
  Simulation sim;
  InitMatch(sim);
  Player* striker = SimulationAccess::RosterOf(sim, 0).Players()[9];
  Player* teammate = SimulationAccess::RosterOf(sim, 0).Players()[8];
  REQUIRE(striker != nullptr);
  REQUIRE(teammate != nullptr);

  // 22 players hold independent requests; two are shown here.
  PlayerActionRequest shoot;
  shoot.kind = ActionRequestKind::Shoot;
  shoot.direction = Vector3(1, 0, 0);
  shoot.power = 1.0f;
  CHECK(striker->Play(shoot) == RequestOutcome::Accepted);

  PlayerActionRequest pass;
  pass.kind = ActionRequestKind::ShortPass;
  pass.target_player = teammate->GetID();
  CHECK(teammate->Play(pass) == RequestOutcome::Accepted);

  REQUIRE(striker->PendingActionRequest().has_value());
  REQUIRE(teammate->PendingActionRequest().has_value());
  CHECK(striker->PendingActionRequest()->kind == ActionRequestKind::Shoot);
  CHECK(teammate->PendingActionRequest()->kind == ActionRequestKind::ShortPass);
  CHECK(striker->ActionRequestAccepts() == 1);
  CHECK(teammate->ActionRequestAccepts() == 1);

  // A passive reaction uses its own channel and preempts.
  PlayerReaction trip;
  trip.kind = ReactionKind::Trip;
  trip.direction = Vector3(0, -1, 0);
  trip.severity = 1.0f;
  CHECK(striker->React(trip) == RequestOutcome::Accepted);
  REQUIRE(striker->PendingReaction().has_value());
  CHECK(striker->PendingReaction()->kind == ReactionKind::Trip);

  // ANIM-2 A11.7: recording the reaction is atomic with the trip staging. A
  // second trip that loses the guard must not overwrite the first accepted one.
  PlayerReaction second_trip = trip;
  second_trip.trip_type = 5;
  CHECK(striker->React(second_trip) == RequestOutcome::Deferred);
  REQUIRE(striker->PendingReaction().has_value());
  CHECK(striker->PendingReaction()->kind == ReactionKind::Trip);
  CHECK(striker->PendingReaction()->trip_type == trip.trip_type);

  // A non-executable reaction (Bump) must not clobber the staged trip either.
  PlayerReaction bump;
  bump.kind = ReactionKind::Bump;
  bump.direction = Vector3(1, 0, 0);
  bump.severity = 1.0f;
  CHECK(striker->React(bump) == RequestOutcome::Accepted);
  REQUIRE(striker->PendingReaction().has_value());
  CHECK(striker->PendingReaction()->kind == ReactionKind::Trip);
  CHECK(striker->PendingReaction()->trip_type == trip.trip_type);

  // Continuous movement is SetControl's job: Play(Move) is rejected and stages
  // no pending input.
  Player* keeper = SimulationAccess::RosterOf(sim, 0).Players()[0];
  REQUIRE(keeper != nullptr);
  CHECK_FALSE(keeper->PendingActionRequest().has_value());
  PlayerActionRequest move;
  move.kind = ActionRequestKind::Move;
  move.direction = Vector3(0, -1, 0);
  move.speed = 6.0f;
  CHECK(keeper->Play(move) == RequestOutcome::Rejected);
  CHECK_FALSE(keeper->PendingActionRequest().has_value());
  CHECK(keeper->ActionRequestRejects() == 1);

  // Pose() returns a finite final pose for an active player.
  const PoseFrame pose = striker->Pose();
  for (std::size_t j = 0; j < kBodyPartCount; ++j) {
    for (int e = 0; e < 4; ++e) {
      CHECK(std::isfinite(pose.orientations[j].elements[e]));
    }
  }

  // A reset must leave no stale request behind.
  sim.ResetSituation(Vector3(0.0f, 0.0f, 0.0f));
  CHECK_FALSE(striker->PendingActionRequest().has_value());
  CHECK_FALSE(striker->PendingReaction().has_value());
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: one final pose per tick, advancing without a re-read",
          "[runtime][sim]") {
  // ANIM-1.2: the per-player pose is committed once per tick by the animation
  // pass; reading it never advances the animation or consumes RNG.
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[9];
  PlayerControlSet controls;

  const auto same = [](const PoseFrame& a, const PoseFrame& b) {
    for (std::size_t j = 0; j < kBodyPartCount; ++j) {
      for (int e = 0; e < 4; ++e)
        if (a.orientations[j].elements[e] != b.orientations[j].elements[e]) return false;
      for (int c = 0; c < 3; ++c)
        if (a.positions[j].coords[c] != b.positions[j].coords[c]) return false;
    }
    return true;
  };

  const PoseFrame first = player->Pose();
  // Idempotent read within the same tick.
  CHECK(same(first, player->Pose()));

  bool changed = false;
  for (int tick = 0; tick < 60; ++tick) {
    sim.Step(controls);
    const PoseFrame now = player->Pose();
    if (!same(now, first)) changed = true;
    // Still idempotent after the tick.
    CHECK(same(now, player->Pose()));
  }
  // Without any new control input the idle animation still advances the pose.
  CHECK(changed);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: players keep independent animation state across teams",
          "[runtime][sim]") {
  // ANIM-1.5: per-player runtime state, scheduled by stable slot; one player's
  // state never leaks into another and Stop/Init leaves no residue.
  Simulation sim;
  InitMatch(sim);
  Player* h_shoot = SimulationAccess::RosterOf(sim, 0).Players()[9];
  Player* h_pass = SimulationAccess::RosterOf(sim, 0).Players()[8];
  Player* a_dribble = SimulationAccess::RosterOf(sim, 1).Players()[9];
  Player* a_trap = SimulationAccess::RosterOf(sim, 1).Players()[7];

  PlayerActionRequest shoot;
  shoot.kind = ActionRequestKind::Shoot;
  PlayerActionRequest pass;
  pass.kind = ActionRequestKind::ShortPass;
  pass.target_player = h_pass->GetID();
  PlayerActionRequest dribble;
  dribble.kind = ActionRequestKind::Dribble;
  PlayerActionRequest trap;
  trap.kind = ActionRequestKind::Trap;

  CHECK(h_shoot->Play(shoot) == RequestOutcome::Accepted);
  CHECK(h_pass->Play(pass) == RequestOutcome::Accepted);
  CHECK(a_dribble->Play(dribble) == RequestOutcome::Accepted);
  CHECK(a_trap->Play(trap) == RequestOutcome::Accepted);

  REQUIRE(h_shoot->PendingActionRequest().has_value());
  REQUIRE(h_pass->PendingActionRequest().has_value());
  REQUIRE(a_dribble->PendingActionRequest().has_value());
  REQUIRE(a_trap->PendingActionRequest().has_value());
  CHECK(h_shoot->PendingActionRequest()->kind == ActionRequestKind::Shoot);
  CHECK(h_pass->PendingActionRequest()->kind == ActionRequestKind::ShortPass);
  CHECK(a_dribble->PendingActionRequest()->kind == ActionRequestKind::Dribble);
  CHECK(a_trap->PendingActionRequest()->kind == ActionRequestKind::Trap);

  // Re-Init builds fresh actors with no residual request.
  CHECK(sim.Stop());
  InitMatch(sim);
  CHECK_FALSE(
      SimulationAccess::RosterOf(sim, 0).Players()[9]->PendingActionRequest().has_value());
  CHECK_FALSE(
      SimulationAccess::RosterOf(sim, 1).Players()[7]->PendingActionRequest().has_value());
  CHECK(sim.Stop());
}


TEST_CASE("Runtime harness: a controlled movement tape drives real locomotion",
          "[runtime][sim]") {
  // P0: the bounded harness must reach play and execute real Movement, so later
  // ANIM-1 migrations can be verified against a real run, not only unit tests.
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[6];
  const Vector3 start = player->GetPosition();

  std::vector<int> anim_ids;
  int in_play_tick = -1;
  for (int tick = 0; tick < 240; ++tick) {
    PlayerControlSet controls;
    PlayerControl control;
    control.move_direction = Vector3(0, -1, 0);
    control.desired_speed = tick < 200 ? 0.0f : 6.0f;  // idle, then run
    controls.Set(player->GetID(), control);
    sim.Step(controls);
    anim_ids.push_back(player->GetBakedPoseSample().animation_id);
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
  }

  int switches = 0;
  for (std::size_t i = 1; i < anim_ids.size(); ++i) {
    if (anim_ids[i] != anim_ids[i - 1]) ++switches;
  }
  const float moved = player->GetPosition().GetDistance(start);
  std::fprintf(stderr, "[runtime] P0 in_play_tick=%d switches=%d moved=%.2f\n",
               in_play_tick, switches, moved);

  // The harness reached play and the tape produced real locomotion + a switch.
  CHECK(in_play_tick > 0);
  CHECK(moved > 0.5f);
  CHECK(switches > 0);
  // ANIM-1 A6.3a shadow oracle: over the Movement tape the motion-driven
  // selector's choice was a legal legacy candidate in `in` ticks and rejected
  // in `not_in` ticks. A non-zero `not_in` means A6.3b cannot blindly hand
  // Movement selection to the Player: it must reproduce the legacy candidate
  // filter and its fallback pick for those ticks.
  const LocomotionPlaybackAudit& audit = LocomotionPlaybackAuditState();
  std::fprintf(stderr,
               "[runtime] A6.3a shadow checks=%d in=%d not_in=%d clip_match=%d "
               "clip_mismatch=%d fallback=%d\n",
               audit.shadow_checks, audit.shadow_in_candidates,
               audit.shadow_not_in_candidates, audit.clip_matches,
               audit.clip_mismatches, audit.fallback_count);
  CHECK(audit.shadow_checks > 0);
  CHECK(audit.clip_matches + audit.clip_mismatches == audit.shadow_checks);
  CHECK(audit.fallback_count == audit.clip_mismatches);
  // ANIM-1 A6.3b.2: the Movement candidate list is produced by the stateless
  // Animation refinement on the in-play Movement tape (the legacy filter+sort
  // chain was removed for pure Movement; its equivalence was proven per tick in
  // the extraction commit before deletion).
  std::fprintf(stderr, "[runtime] A6.3b.2 movement refinement ticks=%d\n",
               audit.refine_checks);
  // ANIM-1 A6.3b.3 (first cut): the Player-owned `SelectedAction` described the
  // action the executor committed - clip id, duration, start frame, touch frame
  // and touch position - on every install, and the same Tick still sampled the
  // new clip's pose without a one-tick offset.
  std::fprintf(stderr, "[runtime] A6.3b.3 install checks=%d match=%d mismatch=%d\n",
               player->InstallChecks(), player->InstallMatches(),
               player->InstallMismatches());
  CHECK(player->InstallChecks() > 0);
  CHECK(player->InstallMismatches() == 0);
  std::fprintf(stderr, "[runtime] A6.3b.3 cursor checks=%d match=%d mismatch=%d\n",
               player->InstallCursorChecks(), player->InstallCursorMatches(),
               player->InstallCursorMismatches());
  CHECK(player->InstallCursorChecks() > 0);
  CHECK(player->InstallCursorMismatches() == 0);
  // ANIM-1 A6.3d: the compat projection no longer decides the clip or
  // the action time; the installed selection, the action cursor and the sampled
  // pose agree on every tick.
  std::fprintf(stderr, "[runtime] A6.3d authority checks=%d match=%d mismatch=%d\n",
               player->AuthorityChecks(), player->AuthorityMatches(),
               player->AuthorityMismatches());
  CHECK(player->AuthorityChecks() > 0);
  CHECK(player->AuthorityMismatches() == 0);
  CHECK(audit.refine_checks > 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: clip switches install at a matched entry frame",
          "[runtime][sim]") {
  // ANIM-2 A13.1: for Movement, the install no longer always begins at frame 0.
  // The Player threads the entry frame into the install, the action cursor
  // agrees with it, and the reference motion is re-based so the first committed
  // tick after a non-zero entry does not add the skipped root displacement.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  int in_play_tick = -1;
  int prev_start_frame = -1;
  bool saw_nonzero_entry = false;
  bool checked_nonzero_delta = false;
  bool pending_delta = false;
  float max_entry_delta = 0.0f;
  Vector3 install_position;
  for (int tick = 0; tick < 400; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    const bool drive = in_play_tick >= 0 && tick > in_play_tick + 3;
    PlayerControlSet controls;
    PlayerControl control;
    // Turn every 80 ticks so the Movement quadrant (and clip) actually changes.
    control.move_direction = (tick % 80) < 40 ? Vector3(0, -1, 0) : Vector3(1, 0, 0);
    control.desired_speed = drive ? 6.0f : 0.0f;
    controls.Set(player->GetID(), control);
    sim.Step(controls);

    const int start_frame = player->InstalledAction().start_frame;
    if (start_frame != prev_start_frame) {
      if (start_frame > 0) {
        saw_nonzero_entry = true;
        pending_delta = true;
        install_position = player->GetPosition();
      } else {
        pending_delta = false;
      }
      prev_start_frame = start_frame;
    } else if (pending_delta) {
      max_entry_delta = std::max(
          max_entry_delta, (player->GetPosition() - install_position).GetLength());
      checked_nonzero_delta = true;
      pending_delta = false;
    }
  }

  std::fprintf(stderr,
               "[runtime] A13.1 entry saw_nonzero=%d checked=%d max_delta=%.3f "
               "install_mismatch=%d\n",
               saw_nonzero_entry ? 1 : 0, checked_nonzero_delta ? 1 : 0,
               max_entry_delta, player->InstallMismatches());
  CHECK(in_play_tick >= 0);
  CHECK(saw_nonzero_entry);
  CHECK(checked_nonzero_delta);
  CHECK(max_entry_delta < 0.5f);
  CHECK(player->InstallMismatches() == 0);
  CHECK(player->InstallCursorMismatches() == 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: pose and collider stay continuous across clip switches",
          "[runtime][sim]") {
  // ANIM-2 A15: at a matched entry frame the first blended tick must not snap
  // the joints or the collision geometry. Bounded per-tick jumps are the
  // production-visible continuity guarantee.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  int in_play_tick = -1;
  bool have_prev = false;
  PoseFrame prev_pose;
  BakedPlayerBodyCollider prev_geom;
  football::math::Vector3 prev_position;
  float max_jump = 0.0f;
  float max_center_delta = 0.0f;
  for (int tick = 0; tick < 400; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    const bool drive = in_play_tick >= 0 && tick > in_play_tick + 3;
    PlayerControlSet controls;
    PlayerControl control;
    control.move_direction = (tick % 80) < 40 ? Vector3(0, -1, 0) : Vector3(1, 0, 0);
    control.desired_speed = drive ? 6.0f : 0.0f;
    controls.Set(player->GetID(), control);
    sim.Step(controls);

    const PoseFrame now = player->PlaybackPose();
    const BakedPlayerBodyCollider geom = player->GetBakedPoseSample().Geometry();
    if (have_prev) {
      max_jump = std::max(max_jump, MaxJointJump(prev_pose, now));
      max_center_delta = std::max(
          max_center_delta,
          MaxColliderCenterDelta(prev_geom, geom,
                                 player->GetPosition() - prev_position));
    }
    prev_pose = now;
    prev_geom = geom;
    prev_position = player->GetPosition();
    have_prev = true;
  }

  std::fprintf(stderr,
               "[runtime] A15.0 max_jump=%.3f max_center_delta=%.3f\n",
               max_jump, max_center_delta);
  CHECK(in_play_tick >= 0);
  CHECK(max_jump < 0.8f);
  // 0.8 m covers the fastest locomotion/fall pose step; a teleport would be
  // metres. The pose bound is the sharper continuity signal.
  CHECK(max_center_delta < 0.8f);
  CHECK(player->InstallMismatches() == 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: the physics pose sample is the Player playback pose",
          "[runtime][sim]") {
  // ANIM-1 A5: the Player owns the final pose; the body/CCD sample physics reads
  // is exactly the Player's playback pose and base yaw.
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[6];
  PlayerControlSet controls;

  for (int tick = 0; tick < 120; ++tick) {
    sim.Step(controls);
    const PlayerBakedPoseSample sample = player->GetBakedPoseSample();
    const PoseFrame& pose = player->PlaybackPose();
    for (std::size_t j = 0; j < kBodyPartCount; ++j) {
      for (int e = 0; e < 4; ++e) {
        REQUIRE(sample.pose.orientations[j].elements[e] == pose.orientations[j].elements[e]);
      }
      for (int c = 0; c < 3; ++c) {
        REQUIRE(sample.pose.positions[j].coords[c] == pose.positions[j].coords[c]);
      }
    }
    REQUIRE(sample.base_angle == player->PlaybackBaseAngle());
  }
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: possession drives a non-Movement action install",
          "[runtime][sim]") {
  // A6.4 prerequisite: the Movement tape cannot reach Trap/Pass/Shot, so this
  // harness puts the ball on a player and asks for a pass to prove the
  // non-Movement selection path is reachable and observable.
  Simulation sim;
  InitMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[6];

  int in_play_tick = -1;
  int non_movement_installs = 0;
  e_FunctionType last_type = e_FunctionType_None;
  for (int tick = 0; tick < 300; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    const bool drive = in_play_tick >= 0 && tick > in_play_tick + 2;

    PlayerControlSet controls;
    PlayerControl control;
    control.move_direction = Vector3(0, -1, 0);
    control.desired_speed = drive ? 5.0f : 0.0f;
    if (drive) {
      SimulationAccess::SetBallRetainer(sim, player);
      SimulationAccess::ResetBall(sim, player->GetPosition() +
                                           Vector3(0, -0.4f, 0));
      control.action = ControlAction::Dribble;
      control.target_position = player->GetPosition() + Vector3(0, -12.0f, 0);
      control.power = 0.6f;
    }
    controls.Set(player->GetID(), control);
    sim.Step(controls);

    const e_FunctionType type = player->GetSimulationActionState().type;
    if (type != last_type) {
      if (type != e_FunctionType_None && type != e_FunctionType_Movement) {
        ++non_movement_installs;
      }
      last_type = type;
    }
  }

  std::fprintf(stderr,
               "[runtime] A6.4 harness in_play_tick=%d non_movement_installs=%d\n",
               in_play_tick, non_movement_installs);
  CHECK(in_play_tick > 0);
  CHECK(player->InstallMismatches() == 0);
  const LocomotionPlaybackAudit& nm = LocomotionPlaybackAuditState();
  std::fprintf(stderr, "[runtime] A6.4 nm pure_ticks=%d touch_selections=%d\n",
               nm.nm_pure_ticks, player->TouchSelectionSuccesses());
  CHECK(player->TouchSelectionSuccesses() > 0);
  CHECK(nm.nm_pure_ticks > 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a controlled pass publishes a real touch",
          "[runtime][sim]") {
  // ANIM-1 A9.6d: drive the Player's own pass execution (no Humanoid) and prove
  // both halves still work: the pass action installs and a real touch is
  // published, so the Player's team is recorded as the last toucher.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  // ANIM-2 A10.4c: the touch selection is now Animation::MatchTouch, driven
  // through the Player's context and query port.

  const football::sim::event::TouchState& touches = SimulationAccess::TouchesOf(sim);
  int in_play_tick = -1;
  int pass_seen_tick = -1;
  bool touched_by_player = false;
  int touches_before = 0;
  // ANIM-2 A16: the pass's entry frame must stay before its touch frame, and
  // the installed contact must match the action state.
  bool touch_install_checked = false;
  int touch_entry_frame = -1;
  int touch_contact_frame = -1;
  int touch_installed_touch_frame = -2;

  for (int tick = 0; tick < 300; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    const bool drive = in_play_tick >= 0 && tick > in_play_tick + 2;

    PlayerControlSet controls;
    PlayerControl control;
    control.move_direction = Vector3(0, -1, 0);
    control.desired_speed = drive ? 3.0f : 0.0f;
    if (drive) {
      // Rotate through action families so the shadow exercises the pass, trap,
      // shot and dribble touch paths. Phases 1 and 3 leave the ball unretained
      // and out of immediate reach so the candidate loop also hits its
      // OutOfReach / AdvantageTooLow rejection branches.
      const int phase = (tick / 50) % 4;
      if (phase == 0) {
        SimulationAccess::SetBallRetainer(sim, player);
        SimulationAccess::ResetBall(sim, player->GetPosition() + Vector3(0, -0.4f, 0));
        control.action = ControlAction::ShortPass;
        control.target_position = player->GetPosition() + Vector3(0, -18.0f, 0);
        control.power = 0.6f;
      } else if (phase == 1) {
        SimulationAccess::SetBallRetainer(sim, nullptr);
        SimulationAccess::ResetBall(sim, player->GetPosition() + Vector3(0, -4.0f, 0));
        control.action = ControlAction::Trap;
      } else if (phase == 2) {
        SimulationAccess::SetBallRetainer(sim, player);
        SimulationAccess::ResetBall(sim, player->GetPosition() + Vector3(0, -0.4f, 0));
        control.action = ControlAction::Shoot;
        control.target_position = player->GetPosition() + Vector3(0, -20.0f, 0);
        control.power = 0.7f;
      } else {
        SimulationAccess::SetBallRetainer(sim, nullptr);
        SimulationAccess::ResetBall(sim, player->GetPosition() + Vector3(0, -8.0f, 0));
        control.action = ControlAction::Dribble;
        control.target_position = player->GetPosition() + Vector3(0, -12.0f, 0);
        control.power = 0.6f;
      }
    }
    controls.Set(player->GetID(), control);
    sim.Step(controls);

    if (player->GetSimulationActionState().type == e_FunctionType_ShortPass &&
        pass_seen_tick < 0) {
      pass_seen_tick = tick;
    }
    const PlayerActionState& action = player->GetSimulationActionState();
    if (!touch_install_checked && action.type == e_FunctionType_ShortPass &&
        action.HasScheduledContact()) {
      touch_entry_frame = player->InstalledAction().start_frame;
      touch_contact_frame = action.ContactFrame();
      touch_installed_touch_frame = player->InstalledAction().touch_frame;
      touch_install_checked = true;
    }
    if (touches.last_player_by_team[player->GetTeamID()] == player->GetID()) {
      touched_by_player = true;
      ++touches_before;
    }
  }
  std::fprintf(stderr, "[runtime] A9.6d in_play=%d pass_tick=%d touches=%d\n",
               in_play_tick, pass_seen_tick, touches_before);
  CHECK(in_play_tick > 0);
  CHECK(pass_seen_tick >= 0);
  CHECK(touched_by_player);
  CHECK(touch_install_checked);
  CHECK(touch_entry_frame >= 0);
  CHECK(touch_entry_frame < touch_contact_frame);
  CHECK(touch_installed_touch_frame == touch_contact_frame);
  CHECK(player->InstallMismatches() == 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a close ball installs a Trap before its touch frame",
          "[runtime][sim]") {
  // ANIM-2 A18.3: the A16/A18 trap path end to end. The entry frame must be
  // before the trap's contact frame and the touch must actually fire.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  int in_play_tick = -1;
  bool trap_checked = false;
  int trap_entry = -1;
  int trap_touch = -1;
  bool trap_touched = false;
  for (int tick = 0; tick < 400; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    const bool drive = in_play_tick >= 0 && tick > in_play_tick + 3;
    PlayerControlSet controls;
    PlayerControl control;
    control.move_direction = Vector3(0, -1, 0);
    control.desired_speed = 0.0f;
    if (drive) {
      SimulationAccess::SetBallRetainer(sim, nullptr);
      SimulationAccess::ResetBall(sim, player->GetPosition() + Vector3(0, -0.3f, 0));
      control.action = ControlAction::Trap;
    }
    controls.Set(player->GetID(), control);
    sim.Step(controls);

    const PlayerActionState& action = player->GetSimulationActionState();
    if (!trap_checked && action.type == e_FunctionType_Trap &&
        action.HasScheduledContact()) {
      trap_entry = player->InstalledAction().start_frame;
      trap_touch = action.ContactFrame();
      trap_checked = true;
    }
    if (trap_checked &&
        SimulationAccess::TouchesOf(sim).last_player_by_team[player->GetTeamID()] ==
            player->GetID()) {
      trap_touched = true;
      break;  // bounded: the trap path is proven once the touch fires
    }
  }

  std::fprintf(stderr, "[runtime] A18.3 trap entry=%d touch=%d touched=%d\n",
               trap_entry, trap_touch, trap_touched ? 1 : 0);
  CHECK(in_play_tick >= 0);
  CHECK(trap_checked);
  CHECK(trap_entry >= 0);
  CHECK(trap_entry < trap_touch);
  CHECK(trap_touched);
  CHECK(player->InstallMismatches() == 0);
  CHECK(sim.Stop());
}


TEST_CASE("Runtime harness: PlayerActionState is the single action time authority",
          "[runtime][sim]") {
  // ANIM-2 A10.8: one action clock. The compatibility frame, the public frame
  // and the playback cursor all track PlayerActionState across a real match,
  // and the playback clip is the installed selection.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  int checked = 0;
  for (int tick = 0; tick < 280; ++tick) {
    PlayerControlSet controls;
    sim.Step(controls);
    if (!player->IsActive()) continue;
    const PlayerActionState& action = player->GetSimulationActionState();
    CHECK(player->GetFrameNum() == action.Frame());
    CHECK(player->GetCurrentFrame() == action.Frame());
    CHECK(player->PlaybackState().frame == action.Frame());
    CHECK(player->PlaybackState().clip_id == player->InstalledAction().clip_id);
    ++checked;
  }
  CHECK(checked > 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a passive trip drives the Trip action chain",
          "[runtime][sim]") {
  // A7: the Player owns the passive-trip decision. Injected directly (so the
  // test does not depend on contact geometry), the accepted trip must reach the
  // animation interrupt and install a Trip action, then leave it again. The tiny
  // roster keeps this reachable inside the runtime timeout.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  // Wait for play: the kickoff resets the players and would clear an earlier
  // pending trip. Inject once the ceremony has finished.
  int in_play_tick = -1;
  int trip_seen_tick = -1;
  int recovery_tick = -1;
  bool trip_injected = false;
  e_FunctionType last_type = e_FunctionType_None;
  std::vector<int> sequence;
  for (int tick = 0; tick < 400 && recovery_tick < 0; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    PlayerControlSet controls;
    sim.Step(controls);
    if (!trip_injected && in_play_tick >= 0 && tick > in_play_tick + 3) {
      // ANIM-2 A11.5: React is the single passive-reaction entry; the staged trip
      // is applied by the next action tick.
      player->React(PlayerReaction{ReactionKind::Trip, Vector3(0, -1, 0), 1.0f,
                                   football::player::kInvalidPlayerId, 2});
      trip_injected = true;
    }
    const e_FunctionType type = player->GetSimulationActionState().type;
    if (type != last_type) {
      sequence.push_back(static_cast<int>(type));
      last_type = type;
    }
    if (type == e_FunctionType_Trip && trip_seen_tick < 0) trip_seen_tick = tick;
    if (trip_seen_tick >= 0 && recovery_tick < 0 && type != e_FunctionType_Trip &&
        type != e_FunctionType_None) {
      recovery_tick = tick;
    }
  }

  std::fprintf(stderr,
               "[runtime] A7 trip in_play=%d trip_tick=%d recovery_tick=%d sequence:",
               in_play_tick, trip_seen_tick, recovery_tick);
  for (int t : sequence) std::fprintf(stderr, " %d", t);
  std::fprintf(stderr, "\n");

  CHECK(in_play_tick >= 0);
  CHECK(trip_injected);
  CHECK(trip_seen_tick >= 0);
  CHECK(recovery_tick >= 0);
  REQUIRE(!sequence.empty());
  CHECK(sequence.back() == static_cast<int>(e_FunctionType_Movement));
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: an accepted Play request executes through the pipeline",
          "[runtime][sim]") {
  // ANIM-2 A11.5: an accepted active request is consumed by the next action tick
  // and runs through the same command -> selection -> install pipeline the
  // controller uses. This proves Play is an execution input, not a write-only
  // record, without depending on contact geometry.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  int in_play_tick = -1;
  bool submitted = false;
  RequestOutcome submit_outcome = RequestOutcome::Rejected;
  bool shot_seen = false;
  for (int tick = 0; tick < 400 && !shot_seen; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    if (in_play_tick >= 0 && tick > in_play_tick + 3) {
      // Keep the ball on the actor so a shot is legal when the request lands.
      SimulationAccess::SetBallRetainer(sim, player);
      SimulationAccess::ResetBall(sim, player->GetPosition() + Vector3(0, -0.4f, 0));
      if (!submitted &&
          player->GetSimulationActionState().type == e_FunctionType_Movement) {
        PlayerActionRequest shoot;
        shoot.kind = ActionRequestKind::Shoot;
        shoot.direction = Vector3(0, -1, 0);
        shoot.power = 0.7f;
        submit_outcome = player->Play(shoot);
        submitted = true;
      }
    }
    PlayerControlSet controls;
    sim.Step(controls);
    if (player->GetSimulationActionState().type == e_FunctionType_Shot) shot_seen = true;
  }

  std::fprintf(stderr, "[runtime] A11.5 in_play=%d shot=%d consumed=%d\n",
               in_play_tick, shot_seen ? 1 : 0,
               player->ActionRequestInputsConsumed());
  CHECK(in_play_tick >= 0);
  CHECK(submitted);
  CHECK(submit_outcome == RequestOutcome::Accepted);
  CHECK(shot_seen);
  CHECK(player->ActionRequestInputsConsumed() > 0);
  CHECK(player->InstallMismatches() == 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a Trip preempts an accepted active request",
          "[runtime][sim]") {
  // ANIM-2 A11.7: once a Trip reaction executes, an accepted Play request must
  // not survive to switch the actor back out of the trip on a later tick.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);

  int in_play_tick = -1;
  bool armed = false;
  bool trip_seen = false;
  bool shot_after_trip = false;
  RequestOutcome play_outcome = RequestOutcome::Rejected;
  RequestOutcome react_outcome = RequestOutcome::Rejected;
  for (int tick = 0; tick < 400; ++tick) {
    if (in_play_tick < 0 && SimulationAccess::IsInPlayOf(sim)) in_play_tick = tick;
    if (!armed && in_play_tick >= 0 && tick > in_play_tick + 3 &&
        player->GetSimulationActionState().type == e_FunctionType_Movement) {
      PlayerActionRequest shoot;
      shoot.kind = ActionRequestKind::Shoot;
      shoot.direction = Vector3(0, -1, 0);
      shoot.power = 0.7f;
      play_outcome = player->Play(shoot);
      react_outcome = player->React(
          PlayerReaction{ReactionKind::Trip, Vector3(0, -1, 0), 1.0f,
                         football::player::kInvalidPlayerId, 2});
      armed = true;
    }
    PlayerControlSet controls;
    sim.Step(controls);
    const e_FunctionType type = player->GetSimulationActionState().type;
    if (type == e_FunctionType_Trip) trip_seen = true;
    if (trip_seen && type == e_FunctionType_Shot) shot_after_trip = true;
    if (trip_seen && type == e_FunctionType_Movement) break;
  }

  std::fprintf(stderr,
               "[runtime] A11.7 preempt play=%d react=%d trip=%d shot_after=%d "
               "preemptions=%d\n",
               static_cast<int>(play_outcome), static_cast<int>(react_outcome),
               trip_seen ? 1 : 0, shot_after_trip ? 1 : 0,
               player->ActionRequestPreemptions());
  CHECK(in_play_tick >= 0);
  CHECK(armed);
  CHECK(play_outcome == RequestOutcome::Accepted);
  CHECK(react_outcome == RequestOutcome::Accepted);
  CHECK(trip_seen);
  CHECK_FALSE(shot_after_trip);
  CHECK(player->ActionRequestPreemptions() > 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: the Player pose preview drives the body forecast",
          "[runtime][sim]") {
  // ANIM-2 A11.0: every match in this harness builds its body forecast through
  // the Player's read-only PreviewRawPose, not by indexing the clip here.
  // ANIM-2 A11.5: this test's own ticks must drive the preview path; do not
  // depend on an earlier test in the same binary having primed the counter.
  const int checks_before = BakedBodyPreviewChecks();

  // ANIM-2 A11.5: the preview is a raw clip pose clamped to the clip range in
  // both directions, and the sample realizes a real, finite collision geometry.
  Simulation sim;
  InitSmallMatch(sim);
  Player* player = SimulationAccess::RosterOf(sim, 0).Players()[1];
  REQUIRE(player != nullptr);
  for (int tick = 0; tick < 40; ++tick) {
    PlayerControlSet controls;
    sim.Step(controls);
  }
  REQUIRE(player->IsActive());

  const int checks = BakedBodyPreviewChecks() - checks_before;
  std::fprintf(stderr, "[runtime] A11.0 preview checks=%d\n", checks);
  CHECK(checks > 0);

  const int last_frame = player->GetFrameCount() - 1;
  REQUIRE(last_frame >= 0);
  const PlayerBakedPoseSample sample = player->GetBakedPoseSample();
  const PlayerBakedPoseSample at_end = player->PreviewRawPose(last_frame + 1000);
  CHECK(at_end.requested_frame == sample.requested_frame + last_frame + 1000);
  CHECK(at_end.sampled_frame == last_frame);
  CHECK(at_end.frame_clamped);

  const PlayerBakedPoseSample at_start = player->PreviewRawPose(-100000);
  CHECK(at_start.requested_frame == sample.requested_frame - 100000);
  CHECK(at_start.sampled_frame == 0);
  CHECK(at_start.frame_clamped);

  // ANIM-2 A11.7: extreme offsets are added in 64 bits, so there is no signed
  // overflow before the clamp.
  const PlayerBakedPoseSample at_max =
      player->PreviewRawPose(std::numeric_limits<int>::max());
  CHECK(at_max.requested_frame ==
        sample.requested_frame + std::numeric_limits<int>::max());
  CHECK(at_max.sampled_frame == last_frame);
  CHECK(at_max.frame_clamped);
  const PlayerBakedPoseSample at_min =
      player->PreviewRawPose(std::numeric_limits<int>::min());
  CHECK(at_min.requested_frame ==
        sample.requested_frame + std::numeric_limits<int>::min());
  CHECK(at_min.sampled_frame == 0);
  CHECK(at_min.frame_clamped);
  const auto geometry = at_end.Geometry();
  for (const auto& shape : geometry.shapes) {
    if (const auto* capsule = std::get_if<football::geometry::Capsule>(&shape)) {
      CHECK(IsFinite(capsule->a));
      CHECK(IsFinite(capsule->b));
      CHECK(capsule->radius > 0.0f);
    } else if (const auto* sphere = std::get_if<football::geometry::Sphere>(&shape)) {
      CHECK(IsFinite(sphere->center));
      CHECK(sphere->radius > 0.0f);
    } else {
      FAIL("unexpected baked body shape");
    }
  }

  // A reset re-installs the action; the preview must still read that first
  // (possibly RNG-chosen) frame consistently with the committed sample.
  player->ResetPosition(player->GetPosition(),
                        player->GetPosition() + Vector3(0, -1, 0));
  const PlayerBakedPoseSample reset_sample = player->GetBakedPoseSample();
  const PlayerBakedPoseSample reset_preview = player->PreviewRawPose(0);
  CHECK(reset_preview.requested_frame == reset_sample.requested_frame);
  CHECK(reset_preview.sampled_frame == reset_sample.sampled_frame);
  CHECK(reset_preview.sampled_frame >= 0);
  CHECK(reset_preview.sampled_frame < player->GetFrameCount());
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: 22 actors share the library deterministically",
          "[runtime][sim]") {
  // ANIM-2 A17: bounded full-roster acceptance. One Simulation owns one
  // AnimationLibrary shared by all 22 actors; a switching tape must produce the
  // same digest on two independent runs.
  struct RunResult {
    std::uint64_t digest = 0;
    int actors = 0;
    int switches = 0;
  };
  const auto run = []() {
    Simulation sim;
    InitMatch(sim);

    std::vector<Player*> actors;
    for (int team = 0; team < 2; ++team) {
      for (Player* p : SimulationAccess::RosterOf(sim, team).Players()) {
        if (p != nullptr) actors.push_back(p);
      }
    }
    std::vector<int> last_anim(actors.size(), -1);

    RunResult result;
    result.actors = static_cast<int>(actors.size());
    std::uint64_t digest = 14695981039346656037ull;
    const auto mix = [&digest](std::uint64_t value) {
      digest ^= value;
      digest *= 1099511628211ull;
    };
    const auto mix_float = [&mix](float value) {
      std::uint32_t bits = 0;
      std::memcpy(&bits, &value, sizeof(bits));
      mix(bits);
    };

    for (int tick = 0; tick < 220; ++tick) {
      PlayerControlSet controls;
      // Drive two actors through the action families so the tape has frequent
      // switches without paying a touch search for all 22; the digest still
      // covers every actor's pose, action and the ball.
      for (std::size_t i = 0; i < actors.size() && i < 2; ++i) {
        PlayerControl control;
        control.move_direction = Vector3(0, -1, 0);
        control.desired_speed = 4.0f;
        const int phase = ((tick / 20) + static_cast<int>(i)) % 4;
        if (phase == 0) {
          control.action = ControlAction::ShortPass;
          control.target_position = actors[i]->GetPosition() + Vector3(0, -10.0f, 0);
          control.power = 0.5f;
        } else if (phase == 1) {
          control.action = ControlAction::Dribble;
          control.target_position = actors[i]->GetPosition() + Vector3(0, -8.0f, 0);
          control.power = 0.5f;
        } else if (phase == 2) {
          control.action = ControlAction::Shoot;
          control.target_position = actors[i]->GetPosition() + Vector3(0, -15.0f, 0);
          control.power = 0.6f;
        } else {
          control.action = ControlAction::Trap;
        }
        controls.Set(actors[i]->GetID(), control);
      }
      sim.Step(controls);

      for (std::size_t i = 0; i < actors.size(); ++i) {
        const PlayerBakedPoseSample sample = actors[i]->GetBakedPoseSample();
        const int anim = static_cast<int>(sample.animation_id);
        if (last_anim[i] >= 0 && anim != last_anim[i]) ++result.switches;
        last_anim[i] = anim;
        mix(static_cast<std::uint64_t>(actors[i]->GetID()));
        mix(static_cast<std::uint64_t>(anim));
        mix(static_cast<std::uint64_t>(actors[i]->GetFrameNum()));
        mix(static_cast<std::uint64_t>(
            actors[i]->GetSimulationActionState().type));
        mix_float(actors[i]->GetPosition().coords[0]);
        mix_float(actors[i]->GetPosition().coords[1]);
        mix_float(actors[i]->GetPosition().coords[2]);
        // ANIM-2 A18.3: the final pose itself, so the digest also pins the pose
        // computation, not only the clip cursor.
        const PoseFrame pose = actors[i]->PlaybackPose();
        for (std::size_t j = 0; j < kBodyPartCount; ++j) {
          for (int e = 0; e < 4; ++e) mix_float(pose.orientations[j].elements[e]);
          mix_float(pose.positions[j].coords[0]);
          mix_float(pose.positions[j].coords[1]);
          mix_float(pose.positions[j].coords[2]);
        }
      }
      const football::math::Vector3 ball =
          SimulationAccess::BallOf(sim)->state().position;
      mix_float(ball.coords[0]);
      mix_float(ball.coords[1]);
      mix_float(ball.coords[2]);
      // ANIM-2 A18.3: the accepted-touch facts for this tick.
      const football::sim::event::TouchState& touches =
          SimulationAccess::TouchesOf(sim);
      mix(static_cast<std::uint64_t>(touches.last_team));
      mix(static_cast<std::uint64_t>(touches.last_player_by_team[0]));
      mix(static_cast<std::uint64_t>(touches.last_player_by_team[1]));
    }

    result.digest = digest;
    sim.Stop();
    return result;
  };

  const RunResult first = run();
  const RunResult second = run();
  std::fprintf(stderr,
               "[runtime] A17 actors=%d switches=%d digest=%016llx\n",
               first.actors, first.switches,
               static_cast<unsigned long long>(first.digest));
  CHECK(first.actors == 22);
  CHECK(first.switches > 0);
  CHECK(first.digest == second.digest);
  CHECK(first.actors == second.actors);
  CHECK(first.switches == second.switches);
}
