// Bounded physical-invariant and replay tests for a driven full roster.
//
// These are Level 2 Runtime tests: they build the real composition, skip the
// opening ceremony, drive a deterministic control tape and assert physical
// correctness (finite, in-bounds, on the ground), real controller interactions
// and bit-exact replay. They never run a match to half time or full time, and
// the tick budgets stay small so the CTest wall-clock budget is unaffected.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "football/ball/ball_model.hpp"
#include "football/ball/ball_state.hpp"
#include "football/observation/world_state.hpp"
#include "football/pitch/pitch.hpp"
#include "football/player/player_control_set.hpp"
#include "football/runtime/match_config.hpp"
#include "player/player.hpp"
#include "runtime/simulation.hpp"
#include "testing/simulation_access.hpp"

namespace {

using football::math::Vector3;
using football::sim::testing::SimulationAccess;
using namespace football::sim;

bool IsFinite(const Vector3& v) {
  return std::isfinite(v.coords[0]) && std::isfinite(v.coords[1]) &&
         std::isfinite(v.coords[2]);
}

// A full 11-a-side roster with explicit identities and a spread formation.
TeamSetup MakeTeam(const char* name, football::player::PlayerId first_id) {
  TeamSetup team;
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

// Reach an underway half without the opening ceremony, so the bounded tick
// budget is spent on physics rather than the kickoff ritual.
void InitOpenMatch(Simulation& sim) {
  sim.Init(MakeTeam("home", 0), MakeTeam("away", 11), football::pitch::Pitch{},
           MatchOptions{}, football::ball::BallModel{});
  SimulationAccess::ClearRestart(sim);
  SimulationAccess::SetPhase(sim, MatchPhase::FirstHalf);
  SimulationAccess::StartPlay(sim);
  SimulationAccess::StartBallInPlay(sim);
}

// A deterministic tape: every actor orbits its formation start position, so the
// roster keeps moving, keeps colliding and can reach the centre ball, without
// ever leaving the pitch.
struct TapeActor {
  Player* player = nullptr;
  Vector3 anchor;
};

std::vector<TapeActor> ActorsOf(Simulation& sim) {
  std::vector<TapeActor> actors;
  for (int team = 0; team < 2; ++team)
    for (Player* player : SimulationAccess::TeamOf(sim, team).Players())
      if (player->IsActive()) actors.push_back({player, player->GetPosition()});
  return actors;
}

PlayerControlSet Tape(const std::vector<TapeActor>& actors, int tick) {
  PlayerControlSet controls;
  for (const TapeActor& actor : actors) {
    const float phase = 0.04f * static_cast<float>(tick) + 0.7f * actor.player->GetID();
    const Vector3 target =
        actor.anchor + Vector3(2.5f * std::sin(phase), 2.5f * std::cos(phase), 0.0f);
    PlayerControl control;
    control.move_direction =
        (target - actor.player->GetPosition()).GetNormalized(
            Vector3(static_cast<float>(actor.player->GetAttackDirection()), 0.0f, 0.0f));
    control.desired_speed = 4.0f;
    controls.Set(actor.player->GetID(), control);
  }
  return controls;
}

}  // namespace

TEST_CASE("Runtime physics: a driven full roster stays finite and in bounds",
          "[runtime][sim][physics]") {
  Simulation sim;
  InitOpenMatch(sim);
  const std::vector<TapeActor> actors = ActorsOf(sim);
  REQUIRE(actors.size() == 22);
  const football::pitch::Pitch& pitch = SimulationAccess::PitchOf(sim);

  for (int tick = 0; tick < 50; ++tick) {
    sim.Step(Tape(actors, tick));
    if (tick % 10 != 0) continue;

    // Aggregate the invariants into two assertions per sampled tick: the
    // expression-string cost of tens of thousands of Catch2 checks would dwarf
    // the simulation itself in a Debug build.
    bool ball_ok = true;
    const football::ball::BallState ball = SimulationAccess::BallOf(sim)->state();
    ball_ok &= IsFinite(ball.position) && IsFinite(ball.velocity);
    // The ball never falls through the ground and never leaves the stadium.
    ball_ok &= ball.position.coords[2] >= -0.05f;
    ball_ok &= ball.position.coords[2] <= 40.0f;
    ball_ok &= std::fabs(ball.position.coords[0]) <= pitch.half_length() + 6.0f;
    ball_ok &= std::fabs(ball.position.coords[1]) <= pitch.half_width() + 6.0f;
    ball_ok &= ball.velocity.GetLength() <= 100.0f;

    bool players_ok = true;
    for (const TapeActor& actor : actors) {
      const PlayerKinematicState state = actor.player->GetKinematicState();
      players_ok &= IsFinite(state.position) && IsFinite(state.velocity) &&
                    IsFinite(state.bodyFacing);
      // Actors move on the ground plane and stay inside a small margin.
      players_ok &= std::fabs(state.position.coords[2]) < 1e-3f;
      players_ok &= std::fabs(state.position.coords[0]) <= pitch.half_length() + 3.0f;
      players_ok &= std::fabs(state.position.coords[1]) <= pitch.half_width() + 3.0f;
      players_ok &= state.GetSpeed() <= actor.player->GetMaxVelocity() * 1.35f;
    }

    INFO("sampled tick " << tick);
    CHECK(ball_ok);
    CHECK(players_ok);
  }
  CHECK(sim.Stop());
}

TEST_CASE("Runtime physics: the same control tape replays bit-exactly",
          "[runtime][sim][physics]") {
  // Step two independent Simulations in lockstep and compare the full
  // authoritative state every tick: one deterministic schedule with no RNG
  // must reproduce ball, player kinematics, action cursors and animation ids
  // bit-exactly, not only at a few checkpoints.
  Simulation first;
  Simulation second;
  InitOpenMatch(first);
  InitOpenMatch(second);
  const std::vector<TapeActor> actors_first = ActorsOf(first);
  const std::vector<TapeActor> actors_second = ActorsOf(second);
  REQUIRE(actors_first.size() == 22);
  REQUIRE(actors_second.size() == actors_first.size());

  bool identical = true;
  int divergence_tick = -1;
  for (int tick = 0; tick < 30 && divergence_tick < 0; ++tick) {
    first.Step(Tape(actors_first, tick));
    second.Step(Tape(actors_second, tick));
    const football::ball::BallState& ball_first =
        SimulationAccess::BallOf(first)->state();
    const football::ball::BallState& ball_second =
        SimulationAccess::BallOf(second)->state();
    bool same = ball_first.position == ball_second.position;
    same &= ball_first.velocity == ball_second.velocity;
    same &= ball_first.angular_velocity == ball_second.angular_velocity;
    for (std::size_t i = 0; i < actors_first.size(); ++i) {
      Player* a = actors_first[i].player;
      Player* b = actors_second[i].player;
      const PlayerKinematicState& ka = a->GetKinematicState();
      const PlayerKinematicState& kb = b->GetKinematicState();
      same &= ka.position == kb.position;
      same &= ka.velocity == kb.velocity;
      same &= ka.bodyFacing == kb.bodyFacing;
      same &= a->GetSimulationActionState().type ==
              b->GetSimulationActionState().type;
      same &= a->GetSimulationActionState().Frame() ==
              b->GetSimulationActionState().Frame();
      same &= a->GetCurrentAnimationId() == b->GetCurrentAnimationId();
      same &= a->GetCurrentFrame() == b->GetCurrentFrame();
      if (!same) break;
    }
    if (!same) divergence_tick = tick;
    identical &= same;
  }
  INFO("first divergent tick: " << divergence_tick);
  CHECK(identical);
  CHECK(first.Stop());
  CHECK(second.Stop());
}

TEST_CASE("Runtime physics: the ball never falls through the ground",
          "[runtime][sim][physics]") {
  Simulation sim;
  InitOpenMatch(sim);
  const std::vector<TapeActor> actors = ActorsOf(sim);
  const float ball_radius = SimulationAccess::BallOf(sim)->config().radius();

  // Drop the ball from height with heavy downward velocity and spin.
  SimulationAccess::SetBallState(
      sim, football::ball::BallState{Vector3(0.0f, 0.0f, 8.0f),
                                     Vector3(2.0f, 1.0f, -35.0f),
                                     Vector3(30.0f, -20.0f, 15.0f),
                                     football::math::Quaternion{}});
  bool ground_ok = true;
  bool finite_ok = true;
  float highest = 0.0f;
  for (int tick = 0; tick < 25; ++tick) {
    sim.Step(Tape(actors, tick));
    const football::ball::BallState ball = SimulationAccess::BallOf(sim)->state();
    finite_ok &= IsFinite(ball.position) && IsFinite(ball.velocity) &&
                 IsFinite(ball.angular_velocity);
    highest = std::max(highest, ball.position.coords[2]);
    // The ground contact response keeps the ball centre at or above its radius.
    ground_ok &= ball.position.coords[2] >= ball_radius - 0.02f;
  }
  CHECK(finite_ok);
  CHECK(ground_ok);
  // It actually fell: the drop was observed rather than silently clamped away.
  CHECK(highest > 3.0f);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime physics: a body contact never collapses two actors together",
          "[runtime][sim][physics]") {
  Simulation sim;
  InitOpenMatch(sim);
  Player* runner = SimulationAccess::TeamOf(sim, 0).Players()[10];
  Player* target = SimulationAccess::TeamOf(sim, 1).Players()[10];
  REQUIRE(runner != nullptr);
  REQUIRE(target != nullptr);
  runner->ResetPosition(Vector3(0.0f, -1.2f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
  target->ResetPosition(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));

  // The runner drives straight into a stationary opponent.
  bool finite = true;
  bool got_close = false;
  float min_separation = 1e9f;
  const Vector3 start = runner->GetPosition();
  for (int tick = 0; tick < 30; ++tick) {
    PlayerControlSet controls;
    PlayerControl drive;
    drive.move_direction = Vector3(0.0f, 1.0f, 0.0f);
    drive.desired_speed = 6.0f;
    controls.Set(runner->GetID(), drive);
    PlayerControl hold;
    hold.move_direction = Vector3(0.0f, 0.0f, 1.0f);
    hold.desired_speed = 0.0f;
    controls.Set(target->GetID(), hold);
    sim.Step(controls);
    const float separation =
        (runner->GetPosition() - target->GetPosition()).Get2D().GetLength();
    finite &= IsFinite(runner->GetPosition()) && IsFinite(target->GetPosition());
    got_close |= separation < 1.0f;
    min_separation = std::min(min_separation, separation);
  }
  CHECK(finite);
  // The runner really closed the gap, so the contact solver was exercised
  // rather than the two actors simply staying apart.
  CHECK((runner->GetPosition() - start).GetLength() > 0.1f);
  CHECK(got_close);
  // Bodies never fully interpenetrate into a single point.
  CHECK(min_separation > 0.05f);
  CHECK(std::fabs(runner->GetPosition().coords[2]) < 1e-3f);
  CHECK(std::fabs(target->GetPosition().coords[2]) < 1e-3f);
  CHECK(sim.Stop());
}
