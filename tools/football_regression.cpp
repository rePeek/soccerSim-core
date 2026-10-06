#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

#include "gameenv.hpp"
#include "sim/simulation.hpp"
#include "../test/default_ai_fixture.hpp"
#include "sim/match.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/player/legacy_locomotion_command.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_body_facing.hpp"
#include "sim/player/player_locomotion.hpp"
#include "sim/player/player_ground_collider.hpp"
#include "sim/player/player_action_executor.hpp"
#include "sim/player/player_action_volume.hpp"
#include "sim/player/player_body_collider.hpp"
#include "animation/import_hierarchy.hpp"
#include "animation/import_loader.hpp"
#include "sim/player/player_decision_scheduler.hpp"
#include "support/diagnostics/backtrace.hpp"

namespace {


constexpr float kFloatTolerance = 1e-5f;

struct RegressionFailure : std::exception {
  explicit RegressionFailure(const std::string& message) : message(message) {}
  const char* what() const noexcept override { return message.c_str(); }
  std::string message;
};

void Require(bool condition, const std::string& message) {
  if (!condition) throw RegressionFailure(message);
}

void CheckPlayerDecisionScheduler() {
  PlayerDecisionScheduler scheduler;
  Require(scheduler.Due(0, 240),
          "player decision scheduler: first decision must be immediately due");
  scheduler.Commit(0);
  Require(!scheduler.Due(20, 240),
          "player decision scheduler: slow current context fired too early");
  Require(scheduler.Due(20, 20),
          "player decision scheduler: faster current context remained blocked by old cadence");
  scheduler.Commit(20);
  Require(!scheduler.Due(30, 20),
          "player decision scheduler: elapsed time below current cadence fired early");
  Require(scheduler.Due(40, 20),
          "player decision scheduler: current cadence was not due at elapsed threshold");
}

void RequireNear(float actual, float expected, const std::string& label) {
  if (std::fabs(actual - expected) > kFloatTolerance) {
    std::ostringstream message;
    message << label << ": expected " << expected << ", got " << actual;
    throw RegressionFailure(message.str());
  }
}


uint64_t HashBytes(uint64_t hash, const void* bytes, size_t size) {
  const auto* value = static_cast<const unsigned char*>(bytes);
  for (size_t index = 0; index < size; ++index) {
    hash ^= value[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
  return HashBytes(hash, &value, sizeof(value));
}

// Deterministic physical/rule payload, not Match lifetime identity.
// simulation_epoch intentionally differs across otherwise identical matches;
// its equality/copy/non-revival semantics are asserted separately.
uint64_t HashWorld(const WorldState& world) {
  uint64_t hash = HashValue(UINT64_C(1469598103934665603), world.tick);
  hash = HashValue(hash, world.reset_sequence);
  hash = HashValue(hash, world.phase);
  hash = HashValue(hash, world.match_time_ms);
  const auto vector_hash = [](uint64_t hash, const Vector3& value) {
    return HashBytes(hash, value.coords, sizeof(value.coords));
  };
  hash = vector_hash(hash, world.ball_position);
  hash = vector_hash(hash, world.ball_velocity);
  hash = HashValue(hash, world.pitch.length());
  hash = HashValue(hash, world.pitch.width());
  hash = HashValue(hash, world.in_play);
  hash = HashValue(hash, world.in_set_piece);
  hash = HashValue(hash, world.restart);
  hash = HashValue(hash, world.restart_taker.has_value());
  if (world.restart_taker) hash = HashValue(hash, *world.restart_taker);
  hash = HashValue(hash, world.ball_retainer.has_value());
  if (world.ball_retainer) hash = HashValue(hash, *world.ball_retainer);
  for (const auto &team : world.teams) {
    hash = HashValue(hash, team.side);
    hash = HashValue(hash, team.defending_direction);
    hash = HashValue(hash, team.score);
  }
  hash = HashValue(hash, static_cast<uint32_t>(world.players.size()));
  for (const WorldPlayerState& player : world.players) {
    hash = HashValue(hash, player.id);
    hash = HashValue(hash, player.side);
    hash = vector_hash(hash, player.position);
    hash = vector_hash(hash, player.velocity);
    hash = vector_hash(hash, player.facing);
    hash = HashValue(hash, player.active);
    hash = HashValue(hash, player.has_possession);
    hash = HashValue(hash, player.lazy);
    hash = HashValue(hash, player.max_speed);
  }
  return hash;
}

void CheckPlayerKinematics() {
  PlayerKinematicState state;
  PlayerKinematicParameters parameters;
  parameters.maxSpeed = 10.0f;
  parameters.acceleration = 10.0f;
  parameters.braking = 5.0f;
  parameters.maxTurnRate = 1.0f;

  PlayerKinematicInput input;
  input.desiredVelocity = Vector3(10.0f, 0.0f, 0.0f);
  input.desiredFacing = Vector3(1.0f, 0.0f, 0.0f);
  PlayerKinematics::Step(state, input, parameters, 0.1f);

  RequireNear(state.velocity.coords[0], 1.0f,
              "kinematics acceleration velocity");
  RequireNear(state.position.coords[0], 0.1f,
              "kinematics acceleration position");
  RequireNear(state.speed, 1.0f, "kinematics acceleration speed");
  Require(state.facing.GetDotProduct(input.desiredFacing) > 0.0f,
          "kinematics should turn toward the desired facing");

  input.desiredVelocity = Vector3(0);
  PlayerKinematics::Step(state, input, parameters, 0.1f);
  RequireNear(state.velocity.coords[0], 0.5f,
              "kinematics braking velocity");
  RequireNear(state.position.coords[0], 0.15f,
              "kinematics braking position");
  RequireNear(state.velocity.coords[2], 0.0f,
              "kinematics planar velocity");
  RequireNear(state.position.coords[2], 0.0f,
              "kinematics planar position");
}

// H3e1b: the procedural locomotion model. These tests pin the structures the
// legacy measurements justified: strongly asymmetric speed approach, turning
// paid for with speed, rate-limited heading change, velocity-derived facing,
// idle facing fallback and planarity. They also pin that the model stays
// continuous, because the legacy per-tick texture was an animation artifact
// that the new simulation must not reproduce.
void CheckProceduralLocomotion() {
  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = 7.5f;
  parameters.acceleration = 6.0f;
  parameters.braking = 12.0f;
  parameters.maxTurnRate = 6.0f;
  parameters.turnRateSpeedFactor = 0.75f;
  parameters.turnSpeedLoss = 0.5f;
  parameters.idleSpeedThreshold = 0.5f;

  const auto require_planar = [](const PlayerKinematicState& state,
                                 const char* label) {
    RequireNear(state.position.coords[2], 0.0f,
                std::string(label) + " planar position");
    RequireNear(state.velocity.coords[2], 0.0f,
                std::string(label) + " planar velocity");
    RequireNear(state.facing.coords[2], 0.0f,
                std::string(label) + " planar facing");
  };

  // Continuous acceleration: one tick reaches acceleration * dt, nowhere near
  // the desired speed. An animation switch would have snapped instead.
  {
    PlayerKinematicState state;
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(7.5f, 0.0f, 0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    RequireNear(state.speed, 0.06f,
                "procedural locomotion: first tick speed");
    RequireNear(state.position.GetLength(), 0.0006f,
                "procedural locomotion: first tick distance");
    require_planar(state, "procedural locomotion acceleration");
  }

  // Braking uses the braking rate, not the acceleration rate: the asymmetry
  // measured in legacy is a deliberate part of the model.
  {
    PlayerKinematicState state;
    state.velocity = Vector3(5.0f, 0.0f, 0.0f);
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    state.speed = 5.0f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.1f);
    RequireNear(state.speed, 3.8f,
                "procedural locomotion: braking uses the braking rate");
    require_planar(state, "procedural locomotion braking");
  }

  // The target is reached exactly, never overshot.
  {
    PlayerKinematicState state;
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(1.0f, 0.0f, 0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 1.0f);
    RequireNear(state.speed, 1.0f,
                "procedural locomotion: target is not overshot");
    require_planar(state, "procedural locomotion target");
  }

  // Turning costs speed: a reversal has half the target speed, while a
  // straight command from the same state keeps full speed.
  {
    PlayerKinematicState turning;
    turning.velocity = Vector3(7.5f, 0.0f, 0.0f);
    turning.facing = Vector3(1.0f, 0.0f, 0.0f);
    turning.speed = 7.5f;
    PlayerLocomotionInput reversal;
    reversal.desiredVelocity = Vector3(-7.5f, 0.0f, 0.0f);
    reversal.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(turning, reversal, parameters, 0.01f);
    RequireNear(turning.speed, 7.38f,
                "procedural locomotion: turning costs speed");

    PlayerKinematicState straight;
    straight.velocity = Vector3(7.5f, 0.0f, 0.0f);
    straight.facing = Vector3(1.0f, 0.0f, 0.0f);
    straight.speed = 7.5f;
    PlayerLocomotionInput ahead;
    ahead.desiredVelocity = Vector3(7.5f, 0.0f, 0.0f);
    ahead.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(straight, ahead, parameters, 0.01f);
    RequireNear(straight.speed, 7.5f,
                "procedural locomotion: straight running keeps speed");
    require_planar(turning, "procedural locomotion turn penalty");
  }

  // The heading is rate limited. At full speed the model turns far more
  // slowly than the requested 90 degrees, which is the structure that
  // replaces legacy's animation-driven instant turns.
  {
    PlayerKinematicState state;
    state.velocity = Vector3(7.5f, 0.0f, 0.0f);
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    state.speed = 7.5f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(0.0f, 7.5f, 0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    const radian turned =
        std::fabs(Vector3(1.0f, 0.0f, 0.0f).GetAngle2D(
            state.velocity.GetNormalized(Vector3(1.0f, 0.0f, 0.0f))));
    RequireNear(turned, 0.015f,
                "procedural locomotion: heading is rate limited");
    require_planar(state, "procedural locomotion turn rate");
  }

  // The actor turns TOWARDS its target. Measured the other way round the model
  // rotates away from it, settles into a circling equilibrium with a permanent
  // ~pi heading error and never arrives, so this case is load bearing.
  {
    PlayerKinematicState state;
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(0.0f, 7.5f, 0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    Require(state.velocity.coords[1] > 0.0f,
            "procedural locomotion: the actor must turn towards its target");
    // Facing only follows once the actor is actually moving, so it is checked
    // after enough steps to leave the idle threshold.
    for (int step = 0; step < 50; ++step) {
      PlayerLocomotion::Step(state, input, parameters, 0.01f);
    }
    Require(state.facing.coords[1] > 0.0f,
            "procedural locomotion: facing must follow a turned velocity");

    // And it converges: aiming at a fixed off-axis target, the actor passes
    // within reach of it at full speed instead of orbiting at half speed. The
    // model has no "arrive and stop" behaviour, so the rollout is judged on its
    // closest approach, which is also what the planner's arrival check asks.
    const Vector3 target(0.0f, 8.0f, 0.0f);
    PlayerKinematicState running;
    running.facing = Vector3(1.0f, 0.0f, 0.0f);
    float closest = 1e9f;
    float fastest = 0.0f;
    for (int step = 0; step < 300; ++step) {
      PlayerLocomotionInput chase;
      chase.desiredVelocity =
          (target.Get2D() - running.position).GetNormalized(running.facing) *
          7.5f;
      chase.idleFacing = running.facing;
      PlayerLocomotion::Step(running, chase, parameters, 0.01f);
      closest = std::min(closest,
                         (target.Get2D() - running.position).GetLength());
      fastest = std::max(fastest, running.speed);
    }
    Require(closest <= 0.9f,
            "procedural locomotion: an off-axis target must be reached");
    RequireNear(fastest, 7.5f,
                "procedural locomotion: convergence reaches full speed");
    require_planar(running, "procedural locomotion turn convergence");
  }

  // Locomotion facing is the velocity direction while moving, which is the
  // legacy structural invariant, even if the state started with a stale
  // facing.
  {
    PlayerKinematicState state;
    state.velocity = Vector3(3.0f, 0.0f, 0.0f);
    state.facing = Vector3(0.0f, -1.0f, 0.0f);
    state.speed = 3.0f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(3.0f, 0.0f, 0.0f);
    input.idleFacing = Vector3(0.0f, -1.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    Require(state.facing.GetDistance(
                state.velocity.GetNormalized(Vector3(1.0f, 0.0f, 0.0f))) <
                1e-6f,
            "procedural locomotion: facing is the velocity direction");
    require_planar(state, "procedural locomotion facing");
  }

  // Standing still holds the idle facing and does not drift.
  {
    PlayerKinematicState state;
    state.position = Vector3(2.0f, 3.0f, 0.0f);
    state.velocity = Vector3(0.0f);
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    state.speed = 0.0f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(0.0f);
    input.idleFacing = Vector3(0.0f, -1.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    RequireNear(state.speed, 0.0f, "procedural locomotion: idle speed");
    RequireNear(state.position.coords[0], 2.0f,
                "procedural locomotion: idle does not drift x");
    RequireNear(state.position.coords[1], 3.0f,
                "procedural locomotion: idle does not drift y");
    RequireNear(state.facing.coords[1], -1.0f,
                "procedural locomotion: idle facing fallback");
    require_planar(state, "procedural locomotion idle");
  }
}

// H3e1c-2: planning and execution must share one physics primitive. Predict()
// is repeated Step(), and reachability is derived from the same Step(), so a
// planner cannot invent its own acceleration or turning rule.
void CheckProceduralLocomotionPrediction() {
  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = 7.5f;
  parameters.acceleration = 6.0f;
  parameters.braking = 12.0f;

  PlayerKinematicState state;
  state.facing = Vector3(1.0f, 0.0f, 0.0f);
  PlayerLocomotionInput input;
  input.desiredVelocity = Vector3(7.5f, 0.0f, 0.0f);
  input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);

  // Predict() must be bit-identical to calling Step() in a loop.
  const PlayerKinematicState predicted =
      PlayerLocomotion::Predict(state, input, parameters, 100);
  PlayerKinematicState manual = state;
  for (int step = 0; step < 10; ++step) {
    PlayerLocomotion::Step(manual, input, parameters, 0.01f);
  }
  Require(predicted.position.coords[0] == manual.position.coords[0] &&
          predicted.velocity.coords[0] == manual.velocity.coords[0],
          "procedural prediction: Predict must be repeated Step");
  RequireNear(predicted.speed, 0.6f,
              "procedural prediction: ramped speed after 100 ms");

  // Reachability follows the same model.
  const Vector3 reachable(5.0f, 0.0f, 0.0f);
  const int eta =
      PlayerLocomotion::EstimateArrival(state, reachable, parameters, 7.5f,
                                        2000, 0.9f, 0.9f).usual_ms;
  Require(eta > 0, "procedural prediction: a reachable target needs an ETA");
  const PlayerKinematicState at_eta = PlayerLocomotion::Predict(
      state, input, parameters, eta);
  Require(at_eta.position.GetDistance(reachable) <= 0.9f + 0.2f,
          "procedural prediction: the ETA must actually arrive");

  // A standstill cannot reach anything, and asking to try must not loop.
  Require(PlayerLocomotion::EstimateArrival(state, reachable, parameters,
                                            0.0f, 2000, 0.9f, 0.9f)
              .usual_ms == -1,
          "procedural prediction: no speed means no reachability");
  // A target beyond the horizon is reported unreachable, not guessed.
  Require(PlayerLocomotion::EstimateArrival(
              state, Vector3(5000.0f, 0.0f, 0.0f), parameters, 7.5f, 500,
              0.9f, 0.9f)
              .usual_ms == -1,
          "procedural prediction: beyond the horizon means unreachable");
  // A target already within reach is immediate.
  Require(PlayerLocomotion::EstimateArrival(
              state, Vector3(0.2f, 0.0f, 0.0f), parameters, 7.5f, 2000,
              0.9f, 0.9f)
              .usual_ms == 0,
          "procedural prediction: an in-reach target is immediate");
}

// H3e1c-3: the intercept variant. The AI consumes "when can I first reach the
// ball", not "how long to reach this fixed point", so the intercept estimator
// is the quantity that actually has to agree with execution.
void CheckProceduralInterceptPrediction() {
  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = 7.5f;
  const float usual_radius = 0.28f;
  const float optimistic_radius = 0.9f;
  const int horizon_ms = 2000;

  PlayerKinematicState state;
  state.facing = Vector3(1.0f, 0.0f, 0.0f);

  const Vector3 stationary(4.0f, 0.0f, 0.0f);
  const PlayerLocomotionReach stationary_reach =
      PlayerLocomotion::EstimateEarliestInterceptExact(
          state, [&stationary](int) { return stationary; }, parameters,
          7.5f, horizon_ms, usual_radius, optimistic_radius);
  Require(stationary_reach.optimistic_ms > 0 &&
              stationary_reach.usual_ms >= stationary_reach.optimistic_ms,
          "procedural intercept: a reachable ball needs a dual estimate");

  // A ball drifting away is caught later than a stationary one.
  const PlayerLocomotionReach drifting_reach =
      PlayerLocomotion::EstimateEarliestInterceptExact(
          state,
          [&stationary](int elapsed_ms) {
            return stationary +
                   Vector3(2.0f, 0.0f, 0.0f) * (elapsed_ms * 0.001f);
          },
          parameters, 7.5f, horizon_ms, usual_radius, optimistic_radius);
  Require(drifting_reach.optimistic_ms > stationary_reach.optimistic_ms ||
              drifting_reach.optimistic_ms < 0,
          "procedural intercept: a moving ball must not be easier");

  // A ball that outruns the actor is unreachable rather than optimistically
  // estimated.
  const PlayerLocomotionReach outrun_reach =
      PlayerLocomotion::EstimateEarliestInterceptExact(
          state,
          [&stationary](int elapsed_ms) {
            return stationary +
                   Vector3(30.0f, 0.0f, 0.0f) * (elapsed_ms * 0.001f);
          },
          parameters, 7.5f, horizon_ms, usual_radius, optimistic_radius);
  Require(outrun_reach.usual_ms == -1 && outrun_reach.optimistic_ms == -1,
          "procedural intercept: a ball the actor cannot outrun is "
          "unreachable");

  // No speed means no intercept.
  Require(PlayerLocomotion::EstimateEarliestInterceptExact(
              state, [&stationary](int) { return stationary; }, parameters,
              0.0f, horizon_ms, usual_radius, optimistic_radius)
              .usual_ms == -1,
          "procedural intercept: no speed means no intercept");

  // The defining case: the ball crosses in front of the actor, so chasing its
  // current position is not the same as intercepting it. The estimator must
  // find the lead point, and must not be worse than pure pursuit.
  const auto crossing_ball = [](int elapsed_ms) {
    return Vector3(3.0f, 2.0f + 2.0f * (elapsed_ms * 0.001f), 0.0f);
  };
  const PlayerLocomotionReach lead_reach =
      PlayerLocomotion::EstimateEarliestInterceptExact(
          state, crossing_ball, parameters, 7.5f, horizon_ms, usual_radius,
          optimistic_radius);
  Require(lead_reach.optimistic_ms > 0 &&
              lead_reach.optimistic_ms < horizon_ms,
          "procedural intercept: the lead point must be reachable");

  // The lead point the estimator chose is genuinely reachable: aiming at that
  // fixed point from the same start arrives inside the intercept time.
  const Vector3 lead_point = crossing_ball(lead_reach.optimistic_ms);
  const PlayerLocomotionReach lead_arrival = PlayerLocomotion::EstimateArrival(
      state, lead_point, parameters, 7.5f, lead_reach.optimistic_ms,
      usual_radius, optimistic_radius);
  Require(lead_arrival.optimistic_ms >= 0,
          "procedural intercept: the chosen lead point must be reachable");

  // Pure pursuit, kept here only as the comparison that documents why the
  // pursuit formulation was replaced.
  int pursuit_ms = -1;
  {
    PlayerKinematicState pursuit = state;
    for (int elapsed = 0; elapsed <= horizon_ms; elapsed += 10) {
      const Vector3 ball = crossing_ball(elapsed).Get2D();
      if ((ball - pursuit.position).GetLength() <= optimistic_radius) {
        pursuit_ms = elapsed;
        break;
      }
      PlayerLocomotionInput input;
      input.desiredVelocity =
          (ball - pursuit.position).GetNormalized(pursuit.facing) * 7.5f;
      input.idleFacing = pursuit.facing;
      PlayerLocomotion::Step(pursuit, input, parameters, 0.01f);
    }
  }
  Require(pursuit_ms < 0 || lead_reach.optimistic_ms <= pursuit_ms,
          "procedural intercept: the lead point must not be worse than pure "
          "pursuit");
}
void CheckPlayerKinematicMirror() {
  PlayerKinematicState state;
  state.position = Vector3(1.0f, 2.0f, 0.0f);
  state.velocity = Vector3(3.0f, 4.0f, 0.0f);
  state.facing = Vector3(0.6f, 0.8f, 0.0f);
  state.bodyFacing = Vector3(-0.8f, 0.6f, 0.0f);
  state.speed = 5.0f;

  state.Mirror();
  RequireNear(state.position.coords[0], -1.0f, "mirror position x");
  RequireNear(state.position.coords[1], -2.0f, "mirror position y");
  RequireNear(state.velocity.coords[0], -3.0f, "mirror velocity x");
  RequireNear(state.velocity.coords[1], -4.0f, "mirror velocity y");
  // Legacy spatial-state mirroring negates position and movement only, so a
  // mirrored kinematic state must keep facing and body facing untouched.
  RequireNear(state.facing.coords[0], 0.6f, "mirror facing x");
  RequireNear(state.facing.coords[1], 0.8f, "mirror facing y");
  RequireNear(state.bodyFacing.coords[0], -0.8f, "mirror body facing x");
  RequireNear(state.bodyFacing.coords[1], 0.6f, "mirror body facing y");
  RequireNear(state.speed, 5.0f, "mirror speed");
}

// H3e3b-prep4: test the body model's designed semantics, not animation pose:
// finite turn rate is always hard; a fixed bounded target converges; and a
// discontinuous locomotion-facing input never snaps the torso but recovers.
void CheckPlayerBodyFacing() {
  PlayerKinematicState state;
  state.facing = Vector3(0.0f, -1.0f, 0.0f);
  state.bodyFacing = state.facing;
  PlayerBodyFacingInput input;
  input.desiredFacing = Vector3(1.0f, 0.0f, 0.0f);
  PlayerBodyFacingParameters parameters;
  parameters.maxTurnRate = 1.0f;
  parameters.maxRelativeAngle = 0.5f;
  const float dt = 0.1f;

  const Vector3 allowed = PlayerBodyFacing::AllowedTarget(state, input, parameters);
  RequireNear(std::fabs(allowed.GetAngle2D(state.facing)), 0.5f,
              "body facing: desired target must be relative-angle clamped");
  for (int tick = 0; tick < 10; ++tick) {
    const Vector3 previous = state.bodyFacing;
    PlayerBodyFacing::Step(state, input, parameters, dt);
    Require(std::fabs(state.bodyFacing.GetAngle2D(previous)) <=
            parameters.maxTurnRate * dt + kFloatTolerance,
            "body facing: turn-rate invariant violated");
    Require(std::fabs(state.bodyFacing.GetAngle2D(state.facing)) <=
            parameters.maxRelativeAngle + kFloatTolerance,
            "body facing: fixed target must stay in its cone");
  }
  RequireNear(std::fabs(state.bodyFacing.GetAngle2D(state.facing)), 0.5f,
              "body facing: fixed target must converge to the allowed target");

  // A 180 degree locomotion-facing jump makes the old body temporarily outside
  // the new cone. Recovery must still be continuous and eventually re-enter it.
  PlayerKinematicState jumped;
  jumped.facing = Vector3(0.0f, -1.0f, 0.0f);
  jumped.bodyFacing = jumped.facing;
  jumped.facing = Vector3(0.0f, 1.0f, 0.0f);
  PlayerBodyFacingInput jumpInput;
  jumpInput.desiredFacing = jumped.facing;
  bool observedOutside = false;
  for (int tick = 0; tick < 40; ++tick) {
    const Vector3 previous = jumped.bodyFacing;
    PlayerBodyFacing::Step(jumped, jumpInput, parameters, dt);
    Require(std::fabs(jumped.bodyFacing.GetAngle2D(previous)) <=
            parameters.maxTurnRate * dt + kFloatTolerance,
            "body facing: facing jump must not snap the torso");
    const bool outside = std::fabs(
        jumped.bodyFacing.GetAngle2D(jumped.facing)) >
        parameters.maxRelativeAngle + kFloatTolerance;
    observedOutside = observedOutside || outside;
  }
  Require(observedOutside,
          "body facing: facing jump should exercise transient cone violation");
  Require(std::fabs(jumped.bodyFacing.GetAngle2D(jumped.facing)) <=
          parameters.maxRelativeAngle + kFloatTolerance,
          "body facing: fixed post-jump target must re-enter the cone");
  RequireNear(std::fabs(jumped.bodyFacing.GetAngle2D(jumped.facing)), 0.0f,
              "body facing: fixed post-jump target must converge");
}

void CheckPlayerGroundCollider() {
  PlayerGroundCollider first;
  first.SetCenter(Vector3(0.0f, 0.0f, 2.0f));
  RequireNear(first.center.coords[2], 0.0f,
              "ground collider should stay on the pitch plane");

  PlayerGroundCollider second;
  second.SetCenter(Vector3(0.71f, 0.0f, 0.0f));
  Require(first.Intersects(second),
          "ground colliders should intersect within their radii");

  second.SetCenter(Vector3(0.72f, 0.0f, 0.0f));
  Require(!first.Intersects(second),
          "ground colliders should not intersect at the radius boundary");
}

void CheckPlayerActionExecutor() {
  PlayerActionDefinition definition;
  definition.type = e_FunctionType_Shot;
  definition.durationTime_ms = 300;
  definition.contactTime_ms = 120;
  definition.contactPosition = Vector3(1.0f, -2.0f, 0.5f);

  PlayerActionState state;
  PlayerActionExecutor::Begin(state, definition);
  Require(state.type == e_FunctionType_Shot, "action executor type");
  Require(state.frame == 0 && state.frameCount == 30,
          "action executor initial frames");
  Require(state.elapsedTime_ms == 0 && state.durationTime_ms == 300,
          "action executor initial timing");
  Require(state.contactFrame == 12 && state.contactTime_ms == 120,
          "action executor contact timing");
  RequireNear(state.contactPosition.coords[0], 1.0f,
              "action executor contact position x");
  Require(state.IsContactPending() && !state.IsContactDue() &&
              !state.IsComplete(),
          "action executor initial phase");

  const PlayerActionStepResult beforeContact =
      PlayerActionExecutor::Step(state, 100);
  Require(state.elapsedTime_ms == 100 && state.frame == 10 &&
              state.IsContactPending(),
          "action executor pre-contact phase");
  Require(!beforeContact.contactTriggered && !beforeContact.completed,
          "action executor should not emit an early event");

  const PlayerActionStepResult atContact =
      PlayerActionExecutor::Step(state, 20);
  Require(state.elapsedTime_ms == 120 && state.frame == 12 &&
              !state.IsContactPending() && state.IsContactDue(),
          "action executor contact phase");
  Require(atContact.contactTriggered && !atContact.completed,
          "action executor should emit one contact event");

  const PlayerActionStepResult atCompletion =
      PlayerActionExecutor::Step(state, 500);
  Require(state.elapsedTime_ms == 300 && state.frame == 30 &&
              state.IsComplete(),
          "action executor completion phase");
  Require(!atCompletion.contactTriggered && atCompletion.completed,
          "action executor should emit one completion event");
}

// H3e1a: the exact authority boundary for procedural locomotion. Only plain
// Movement ticks with no scheduled contact and no ball retention may leave
// the animation root-motion path; everything else must keep it because the
// animation root also feeds the touch vector and impulse algorithms.
void CheckPureLocomotionBoundary() {
  PlayerActionState movement;
  movement.type = e_FunctionType_Movement;
  Require(movement.IsPureLocomotion(false),
          "pure locomotion: plain movement should be eligible");
  Require(!movement.IsPureLocomotion(true),
          "pure locomotion: retaining the ball must stay animation-driven");

  PlayerActionState contact = movement;
  contact.contactTime_ms = 120;
  contact.contactFrame = 12;
  Require(!contact.IsPureLocomotion(false),
          "pure locomotion: a scheduled contact must stay animation-driven");

  const e_FunctionType animated[] = {
      e_FunctionType_None,       e_FunctionType_BallControl,
      e_FunctionType_Trap,       e_FunctionType_ShortPass,
      e_FunctionType_LongPass,   e_FunctionType_HighPass,
      e_FunctionType_Header,     e_FunctionType_Shot,
      e_FunctionType_Deflect,    e_FunctionType_Catch,
      e_FunctionType_Interfere,  e_FunctionType_Trip,
      e_FunctionType_Sliding,    e_FunctionType_Special};
  for (e_FunctionType type : animated) {
    PlayerActionState action;
    action.type = type;
    Require(!action.IsPureLocomotion(false),
            "pure locomotion: a non-locomotion action must stay "
            "animation-driven");
  }
}

// H3e1c-1: the legacy command adapter. Legacy commands are continuous, but the
// old controller resolved anything below the idle switch to an idle animation
// whose root motion was effectively zero. That deadband is command semantics,
// so it lives in the adapter and not in PlayerLocomotion. Above it the speed
// stays continuous and must not be snapped to the dribble/walk/sprint vertex
// values, because the class is an animation-selection regime and not a
// physical quantity.
void CheckLegacyLocomotionCommandAdapter() {
  PlayerKinematicState state;
  state.position = Vector3(1.0f, 2.0f, 0.0f);
  state.facing = Vector3(1.0f, 0.0f, 0.0f);
  const Vector3 body_facing(0.0f, -1.0f, 0.0f);
  const float max_speed = 8.0f;

  PlayerCommand command;
  command.useDesiredMovement = true;
  command.desiredDirection = Vector3(1.0f, 0.0f, 0.0f);

  // Below the idle switch the actor must stand still.
  command.desiredVelocityFloat = 0.5f;
  Require(FloatToEnumVelocity(0.5f) == e_Velocity_Idle,
          "legacy adapter test sanity: 0.5 m/s is the idle class");
  {
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), 0.0f,
                "legacy adapter: idle deadband");
    RequireNear(input.idleFacing.coords[1], -1.0f,
                "legacy adapter: body facing fallback");
  }

  // Above the deadband the speed is continuous: the class is a selection
  // regime, so 4.5 m/s must not become the walk vertex of 5.0.
  const float continuous_speeds[] = {1.8f, 2.6f, 3.4f, 4.5f, 5.7f, 6.9f, 8.0f};
  for (float speed : continuous_speeds) {
    Require(FloatToEnumVelocity(speed) != e_Velocity_Idle,
            "legacy adapter test sanity: speed above the deadband");
    command.desiredVelocityFloat = speed;
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), speed,
                "legacy adapter: continuous speed is not snapped");
  }

  // The actor's own maximum speed still caps the command.
  command.desiredVelocityFloat = 100.0f;
  {
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), max_speed,
                "legacy adapter: speed is clamped to the actor maximum");
  }

  // desiredLookAt only supplies the facing to hold while standing.
  command.desiredVelocityFloat = 0.1f;
  command.useDesiredLookAt = true;
  command.desiredLookAt = Vector3(1.0f, 4.0f, 0.0f);
  {
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), 0.0f,
                "legacy adapter: lookAt does not create locomotion");
    RequireNear(input.idleFacing.coords[0], 0.0f,
                "legacy adapter: lookAt facing x");
    RequireNear(input.idleFacing.coords[1], 1.0f,
                "legacy adapter: lookAt facing y");
  }
}

void CheckPlayerActionVolume() {
  PlayerActionState action;
  PlayerKinematicState kinematics;
  kinematics.position = Vector3(0.0f, 0.0f, 0.0f);
  kinematics.facing = Vector3(0.0f, -1.0f, 0.0f);

  PlayerActionVolumeParameters parameters;
  parameters.contactWindowStartFrame = 5;
  parameters.contactWindowEndFrame = 28;

  // Non-reaching actions never produce a volume.
  action.type = e_FunctionType_Movement;
  action.frame = 10;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "movement should not have a tackle volume");

  // Outside the contact window the volume is inactive too.
  action.type = e_FunctionType_Sliding;
  action.frame = 5;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "slide before the contact window should be inactive");
  action.frame = 28;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "slide after the contact window should be inactive");

  // Inside the window a slide reaches forward along facing.
  action.frame = 12;
  const PlayerActionVolume slide =
      BuildTackleVolume(action, kinematics, parameters);
  Require(slide.active, "slide inside the contact window should be active");
  RequireNear(slide.axis.coords[1], -1.0f, "slide axis should follow facing y");

  PlayerGroundCollider victim;
  const float reach = parameters.slideReach + parameters.slideRadius +
                      victim.radius;
  victim.SetCenter(Vector3(0.0f, -1.0f, 0.0f));
  Require(slide.Intersects(victim), "slide should reach a victim in front");

  // The capsule starts at the actor, so a victim behind is not reached.
  victim.SetCenter(Vector3(0.0f, 1.0f, 0.0f));
  Require(!slide.Intersects(victim), "slide should not reach behind");

  // Reach plus radii, inclusive boundary.
  victim.SetCenter(Vector3(0.0f, -(reach - 0.01f), 0.0f));
  Require(slide.Intersects(victim), "slide should reach just inside its boundary");
  victim.SetCenter(Vector3(0.0f, -(reach + 0.01f), 0.0f));
  Require(!slide.Intersects(victim), "slide should not reach past its boundary");

  // Standing tackle is shorter and broader.
  action.type = e_FunctionType_Interfere;
  const PlayerActionVolume interfere =
      BuildTackleVolume(action, kinematics, parameters);
  Require(interfere.active, "interfere should be active inside the window");
  Require(interfere.length < slide.length,
          "interfere should reach less far than a slide");
  Require(interfere.radius > slide.radius,
          "interfere should be broader than a slide");

  // Height is ignored: everything is evaluated on the pitch plane.
  victim.SetCenter(Vector3(0.0f, -1.0f, 2.0f));
  Require(slide.Intersects(victim), "tackle volume should be planar");
}

void CheckPlayerBodyCollider() {
  PlayerKinematicState kinematics;
  kinematics.position = Vector3(0.0f, 0.0f, 0.0f);
  const PlayerBodyCollider body = BuildBodyCollider(kinematics);

  Require(body.upperBody.center.coords[2] > body.lowerBody.center.coords[2],
          "torso should sit above the lower body");
  Require(body.head.center.coords[2] > body.upperBody.center.coords[2],
          "head should sit above the torso");

  // Body radii must stay body-sized: the ground contest radius is a contact
  // radius and is larger.
  const PlayerGroundCollider ground;
  Require(body.upperBody.radius < ground.radius,
          "body collider must not reuse the ground contest radius");

  const float ballRadius = 0.11f;

  // A ball beside the legs while the ball is at leg height is a hit.
  Require(body.lowerBody.IntersectsSphere(
              Vector3(body.lowerBody.radius + ballRadius - 0.01f, 0.0f,
                      body.lowerBody.center.coords[2]), ballRadius),
          "lower body should touch a ball beside the legs");

  // The torso sits high up, so it must not claim a ground ball.
  Require(!body.upperBody.IntersectsSphere(Vector3(1.0f, 0.0f, 0.11f), ballRadius),
          "torso should not touch a distant ground ball");
  Require(!body.head.IntersectsSphere(Vector3(0.0f, 0.0f, 0.11f), ballRadius),
          "head should not touch a ground ball");

  // Head-height ball is a head hit, not a leg hit.
  Require(body.head.IntersectsSphere(
              Vector3(0.0f, 0.0f, body.head.center.coords[2]), ballRadius),
          "head should touch a head height ball");
  Require(!body.lowerBody.IntersectsSphere(
              Vector3(0.0f, 0.0f, body.head.center.coords[2]), ballRadius),
          "lower body should not touch a head height ball");

  // The collider follows the player position and stays upright.
  PlayerKinematicState moved = kinematics;
  moved.position = Vector3(3.0f, -4.0f, 0.0f);
  moved.facing = Vector3(1.0f, 0.0f, 0.0f);
  const PlayerBodyCollider movedBody = BuildBodyCollider(moved);
  RequireNear(movedBody.lowerBody.center.coords[0], 3.0f,
              "body collider should follow position x");
  RequireNear(movedBody.lowerBody.center.coords[1], -4.0f,
              "body collider should follow position y");
  Require(!movedBody.upperBody.IntersectsSphere(Vector3(0.0f, 0.0f, 1.11f),
                                               ballRadius),
          "body collider should not stay at the origin");
}

void AppendDigestFloat(std::string& out, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08x", bits);
  out += buf;
}

template <typename T>
void AppendDigestInt(std::string& out, const T& value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld|",
                static_cast<long long>(value));
  out += buf;
}

// A presentation-independent fingerprint of everything the next simulation
// tick may read. Deliberately includes the deterministic RNG *state*, not just
// a draw count: equal draw counts do not imply an equal stream.
std::string CaptureSimulationDigest(Simulation& simulation) {
  std::string out;
  Match* match = simulation.match();

  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  AppendDigestInt(out, players.size());
  for (const Player* actor : players) {
    const PlayerKinematicState& kinematics = actor->GetKinematicState();
    AppendDigestFloat(out, kinematics.position.coords[0]);
    AppendDigestFloat(out, kinematics.position.coords[1]);
    AppendDigestFloat(out, kinematics.velocity.coords[0]);
    AppendDigestFloat(out, kinematics.velocity.coords[1]);
    AppendDigestFloat(out, kinematics.facing.coords[0]);
    AppendDigestFloat(out, kinematics.facing.coords[1]);
    const PlayerGroundCollider& ground = actor->GetGroundCollider();
    AppendDigestFloat(out, ground.center.coords[0]);
    AppendDigestFloat(out, ground.center.coords[1]);
    AppendDigestFloat(out, ground.radius);
    const PlayerActionState& action = actor->GetSimulationActionState();
    AppendDigestInt(out, static_cast<int>(action.type));
    AppendDigestInt(out, action.frame);
    AppendDigestInt(out, action.frameCount);
    AppendDigestInt(out, action.elapsedTime_ms);
    AppendDigestInt(out, action.contactTime_ms);
    AppendDigestFloat(out, action.contactPosition.coords[0]);
    AppendDigestFloat(out, action.contactPosition.coords[1]);
  }

  Ball* ball = match->GetBall();
  const Vector3 ballPos = ball->Predict(0);
  const Vector3 ballMomentum = ball->GetMovement();
  const Vector3 ballRotation = ball->GetRotation();
  AppendDigestFloat(out, ballPos.coords[0]);
  AppendDigestFloat(out, ballPos.coords[1]);
  AppendDigestFloat(out, ballPos.coords[2]);
  AppendDigestFloat(out, ballMomentum.coords[0]);
  AppendDigestFloat(out, ballMomentum.coords[1]);
  AppendDigestFloat(out, ballMomentum.coords[2]);
  AppendDigestFloat(out, ballRotation.coords[0]);
  AppendDigestFloat(out, ballRotation.coords[1]);
  AppendDigestFloat(out, ballRotation.coords[2]);

  AppendDigestInt(out, match->GetScore(0));
  AppendDigestInt(out, match->GetScore(1));
  AppendDigestInt(out, static_cast<int>(match->GetMatchPhase()));
  AppendDigestInt(out, match->IsInPlay() ? 1 : 0);
  AppendDigestInt(out, match->IsInSetPiece() ? 1 : 0);
  AppendDigestInt(out, match->GetLastTouchTeamID());
  AppendDigestInt(out, match->GetLastTouchPlayer() == nullptr ? -1 : 1);
  AppendDigestInt(out, match->GetDesignatedPossessionPlayer() == nullptr ? -1 : 1);
  AppendDigestInt(out, match->GetBallRetainer() == nullptr ? -1 : 1);

  const RefereeBuffer& buffer = match->GetReferee()->GetBuffer();
  AppendDigestInt(out, buffer.active ? 1 : 0);
  AppendDigestInt(out, buffer.stopTime);
  AppendDigestInt(out, buffer.prepareTime);
  AppendDigestInt(out, buffer.startTime);
  AppendDigestInt(out, static_cast<int>(buffer.desiredSetPiece));
  AppendDigestInt(out, match->GetReferee()->GetCurrentFoulType());
  AppendDigestFloat(out, buffer.restartPos.coords[0]);
  AppendDigestFloat(out, buffer.restartPos.coords[1]);

  // Compare the actual deterministic RNG state, not a draw-count surrogate.
  std::ostringstream rngState;
  rngState << match->rng().engine();
  out += rngState.str();

  return out;
}

// A fixture initializer, not an environment adapter or runtime accessor.
void InitDefaultMatch(Simulation& simulation) {
  simulation.Stop();
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                  football::app::fixtures::MakeDefaultAwayTeam(),
                  football::model::MakeLegacyPitch(), MatchOptions{}, false);
}

void Advance(Simulation& simulation, int ticks) {
  const auto policy = football::test::MakeDefaultAI(simulation);
  for (int tick = 0; tick < ticks; ++tick) football::test::StepDefaultAI(simulation, policy);
}

void CheckCanonicalFrame(Simulation& simulation) {
  Match* match = simulation.match();
  Require(!match->isBallMirrored() && !match->GetTeam(0)->isMirrored() &&
              !match->GetTeam(1)->isMirrored(),
          "simulation tick leaked a mirrored frame");
  for (int team_id = 0; team_id < 2; ++team_id) {
    std::vector<Player*> players;
    match->GetTeam(team_id)->GetActivePlayers(players);
    for (Player* player : players) player->CheckSimulationActionOracle();
  }
}

static_assert(std::is_final_v<Player>);
static_assert(!std::is_polymorphic_v<Player>);
static_assert(!std::is_abstract_v<Player>);

void CheckFlattenedPlayerLifecycle(Simulation& simulation) {
  InitDefaultMatch(simulation);
  Advance(simulation, 201);
  Match* match = simulation.match();
  std::vector<Player*> players;
  match->GetTeam(1)->GetActivePlayers(players);
  Player* player = players.at(1);
  player->RelaxFatigue(-0.25f);
  const auto stat = football::model::PlayerStat::physical_reaction;
  float multiplier = 0.3f + 0.7f * player->GetTeam()->GetAiDifficulty();
  multiplier *= 0.7f + 0.3f * player->GetFatigueFactorInv();
  RequireNear(player->GetStat(stat),
              player->GetModel().attributes.get(stat) * multiplier,
              "flattened Player lost difficulty/fatigue-adjusted stats");

  // No runtime actor owns or exposes a decision object.
  static_assert(!football::test::HasDecisionObject<Player>);
  static_assert(!football::test::HasDecisionObject<Team>);
  PlayerControl control;
  control.move_direction = Vector3(1, 0, 0);
  control.desired_speed = dribbleVelocity;
  control.action = ControlAction::Shoot;
  control.power = 0.6f;
  player->SetControl(control);
  PlayerCommandQueue commands;
  player->RequestCommand(commands);
  player->ClearControl();
  Require(commands.size() == 2 &&
              commands.front().desiredFunctionType == e_FunctionType_Shot &&
              commands.front().desiredVelocityFloat == control.desired_speed &&
              commands.front().useTouchInfo &&
              commands.front().touchInfo.inputPower == control.power,
          "flattening changed explicit-control command authority");

  blunted::SimulationRng& rng = match->rng();
  blunted::SimulationRng expected = rng;
  (void)expected();
  (void)expected();
  const auto epoch = player->GetDecisionLocomotionContinuityEpoch();
  player->Deactivate();
  Require(!player->IsActive() &&
              player->GetDecisionLocomotionContinuityEpoch() == epoch + 2 &&
              rng.engine() == expected.engine() &&
              match->GetTeam(1)->GetActivePlayersCount() == 10,
          "flattening changed deactivation's double reset or RNG order");

  // Simulation owns RNG after Stop; compare the teardown draws without
  // retaining any deleted player/Match pointer. Active players reset once,
  // inactive players not at all. Roster callbacks during deletion are unsafe.
  std::vector<Player*> remaining;
  match->GetActiveTeamPlayers(0, remaining);
  match->GetActiveTeamPlayers(1, remaining);
  expected = rng;
  for (std::size_t i = 0; i < remaining.size(); ++i) (void)expected();
  Require(simulation.Stop() && rng.engine() == expected.engine(),
          "flattening changed active-player teardown or its RNG window");
  InitDefaultMatch(simulation);
}

template <class T> concept HasOfficialActors =
    requires { &T::GetOfficials; } || requires { &T::GetOfficialPlayers; };
template <class T> concept HasAnimationRestartHook =
    requires { &T::AlterSetPiecePrepareTime; };
static_assert(!HasOfficialActors<Match>);
static_assert(!HasAnimationRestartHook<Referee>);

// Test-only rule inputs: exercise CheckFoul/Process without relying on a
// particular tackle clip or exposing a production foul-injection API.
class RefereeFixture : public Referee {
 public:
  using Referee::Referee;
  void RecordFoul(Player* offender, Player* victim, int type,
                  const Vector3& position, bool advantage = false) {
    buffer.active = false;
    buffer.endPhase = false;
    buffer.setpiece_team = victim->GetTeam();
    foul = Foul{};
    foul.foulPlayer = offender;
    foul.foulVictim = victim;
    foul.foulType = type;
    foul.foulTime = match->GetActualTime_ms();
    foul.foulPosition = position;
    foul.advantage = advantage;
  }
};

void CheckRefereeRules(Simulation& simulation) {
  // Both clock policies, both restart types, ordinary/yellow/red/second-yellow.
  for (bool animations : {false, true}) {
    for (bool penalty : {false, true}) {
      for (int scenario : {1, 2, 3, 4}) {
        InitDefaultMatch(simulation);
        Advance(simulation, 201);  // Leave the initial kickoff, into open play.
        Match* match = simulation.match();
        Require(match->IsInPlay() && !match->IsInSetPiece(),
                "referee fixture should be in open play");
        std::vector<Player*> home, away;
        match->GetTeam(0)->GetActivePlayers(home);
        match->GetTeam(1)->GetActivePlayers(away);
        Player* offender = home.at(1);
        Player* victim = away.at(1);
        const int foul_type = scenario == 4 ? 2 : scenario;
        if (scenario == 4) offender->GiveYellowCard(0);
        const Vector3 position = penalty
            ? Vector3(-45.0f * victim->GetTeam()->GetStaticSide(), 0, 0)
            : Vector3(0, 0, 0);
        RefereeFixture rules(match, animations);
        // Even advantage must be stopped for a penalty.
        rules.RecordFoul(offender, victim, foul_type, position, penalty);
        const auto rng_before = match->rng().engine();
        const unsigned long stopped = match->GetActualTime_ms();
        Require(rules.CheckFoul(), "unprocessed foul did not stop play");
        const RefereeBuffer scheduled = rules.GetBuffer();
        const unsigned long card_delay = foul_type >= 2 ? 10000 : 0;
        const unsigned long effective_time = match->GetActualTime_ms() + 6000;
        Require(!match->IsInPlay() && scheduled.active &&
                    scheduled.desiredSetPiece == (penalty ? e_GameMode_Penalty
                                                          : e_GameMode_FreeKick) &&
                    scheduled.teamID == victim->GetTeam()->GetID() &&
                    scheduled.stopTime == stopped &&
                    scheduled.prepareTime == stopped + 2000 + card_delay &&
                    scheduled.startTime == scheduled.prepareTime + 2000 &&
                    match->GetActualTime_ms() ==
                        stopped + (animations ? 0 : 1900 + card_delay) &&
                    offender->HasCards() == (foul_type >= 2) &&
                    match->rng().engine() == rng_before,
                "rule restart/card budget changed or consumed RNG");
        Require(!rules.CheckFoul() &&
                    rules.GetBuffer().prepareTime == scheduled.prepareTime &&
                    rules.GetBuffer().startTime == scheduled.startTime,
                "foul was processed twice or its deadline changed");

        // Advancing the rule clock alone cannot extend the card deadline.
        // No official animation/controller participates in this loop.
        while (match->GetActualTime_ms() < scheduled.prepareTime) {
          rules.Process();
          Require(rules.GetBuffer().prepareTime == scheduled.prepareTime &&
                      rules.GetBuffer().startTime == scheduled.startTime &&
                      rules.GetBuffer().taker == nullptr && !match->IsInPlay(),
                  "restart prepared early or waited for an actor");
          match->BumpActualTime_ms(10);
        }
        rules.Process();
        Require(rules.GetBuffer().taker != nullptr && !match->IsInPlay(),
                "restart not prepared at its rule deadline");
        match->BumpActualTime_ms(scheduled.startTime - match->GetActualTime_ms() - 10);
        rules.Process();
        Require(!match->IsInPlay(), "restart whistle was early");
        match->BumpActualTime_ms(10);
        rules.Process();
        Require(match->IsInPlay() && match->IsInSetPiece(),
                "restart whistle needs an official actor");

        // Card state remains gameplay: a red or second yellow sends off a
        // football player, whereas one yellow leaves the player active.
        const auto policy = football::test::MakeDefaultAI(simulation);
        if (match->GetActualTime_ms() < effective_time) {
          football::test::StepDefaultAI(simulation, policy);
          Require(offender->IsActive(), "card took effect before its deadline");
          match->BumpActualTime_ms(effective_time - match->GetActualTime_ms());
        }
        football::test::StepDefaultAI(simulation, policy);
        const bool send_off = scenario == 3 || scenario == 4;
        Require(offender->IsActive() == !send_off &&
                    match->GetTeam(0)->GetActivePlayersCount() == (send_off ? 10 : 11),
                "disciplinary state lost red/second-yellow send-off semantics");
      }
    }
  }

  {
    InitDefaultMatch(simulation);
    Advance(simulation, 201);
    Match* match = simulation.match();
    std::vector<Player*> home, away;
    match->GetTeam(0)->GetActivePlayers(home);
    match->GetTeam(1)->GetActivePlayers(away);
    RefereeFixture advantage(match, false);
    advantage.RecordFoul(home.at(1), away.at(1), 1, Vector3(0), true);
    Require(!advantage.CheckFoul() && match->IsInPlay(),
            "advantage should not immediately stop open play");
    match->BumpActualTime_ms(3010);
    Require(!advantage.CheckFoul() && advantage.GetCurrentFoulType() == 0 &&
                match->IsInPlay(), "expired advantage was not cancelled");
  }

  // Real Match-owned rule engine, no fixture: record an offside pass and
  // reception. Neither positioning nor detection requires a linesman.
  InitDefaultMatch(simulation);
  Advance(simulation, 201);
  Match* match = simulation.match();
  std::vector<Player*> home, away;
  match->GetTeam(0)->GetActivePlayers(home);
  match->GetTeam(1)->GetActivePlayers(away);
  const int side = match->GetTeam(0)->GetDynamicSide();
  for (Player* defender : away)
    defender->ResetPosition(Vector3(-10.0f * side, 0, 0), Vector3(0));
  home.at(1)->ResetPosition(Vector3(0, 0, 0), Vector3(0));
  home.at(2)->ResetPosition(Vector3(-45.0f * side, 0, 0), Vector3(0));
  match->GetBall()->ResetSituation(Vector3(0));
  match->GetTeam(0)->SetLastTouchPlayer(home.at(1));
  Require(match->IsInPlay(), "offside flagged the passer instead of reception");
  match->GetTeam(0)->SetLastTouchPlayer(home.at(2));
  Require(!match->IsInPlay() && match->GetReferee()->GetBuffer().active &&
              match->GetReferee()->GetBuffer().desiredSetPiece == e_GameMode_FreeKick &&
              match->GetReferee()->GetBuffer().teamID == 1,
          "offside detection depended on the deleted linesmen");
}

struct GoldenSnapshot {
  int ticks;
  uint64_t world_hash;
  uint64_t simulation_hash;
};

// Value-policy/home-frame baseline. This intentionally replaces Eliza/RNG
// trajectories; the previous golden pairs are archived in test/baselines/.
// Runner contract adds authoritative phase/football time to HashWorld only.
// Motion/actions/RNG simulation digest goldens remain unchanged; old World hashes
// are recorded in test/baselines/pre_match_runner.md.
constexpr GoldenSnapshot golden[] = {
    {1, UINT64_C(181739816817067074), UINT64_C(350760935552669674)},
    {100, UINT64_C(2189927517799612813), UINT64_C(13725550824419574755)},
    {500, UINT64_C(1266667866899682051), UINT64_C(7040189435179413637)},
    {1000, UINT64_C(6003776441005663363), UINT64_C(10968404906479542722)},
};

void CheckGoldenSnapshots(Simulation& simulation, GameEnv& game, bool print_baseline) {
  InitDefaultMatch(simulation);
  game.Stop();
  game.Start();
  int completed = 0;
  for (const GoldenSnapshot& expected : golden) {
    Advance(simulation, expected.ticks - completed);
    for (; completed < expected.ticks; ++completed) game.Step();
    const WorldState world = simulation.Observe();
    const std::string digest = CaptureSimulationDigest(simulation);
    const uint64_t world_hash = HashWorld(world);
    const uint64_t simulation_hash =
        HashBytes(UINT64_C(1469598103934665603), digest.data(), digest.size());
    CheckCanonicalFrame(simulation);
    Require(world.tick == static_cast<uint64_t>(completed),
            "one step no longer means one simulation tick");
    // Public API coverage stays public: compare a separately owned environment,
    // never inspect its internals or install a compatible diagnostic accessor.
    Require(HashWorld(game.Observe()) == world_hash,
            "GameEnv diverged from direct Simulation at tick " + std::to_string(completed));
    if (print_baseline) {
      std::cout << "    {" << completed << ", UINT64_C(" << world_hash
                << "), UINT64_C(" << simulation_hash << ")},\n";
    } else {
      Require(world_hash == expected.world_hash &&
                  simulation_hash == expected.simulation_hash,
              "core golden mismatch at tick " + std::to_string(completed));
    }
  }
}

void CheckResetDeterminism(Simulation& simulation) {
  InitDefaultMatch(simulation);
  const AnimationLibrary* animations = &simulation.match()->GetAnimationLibrary();
  const uint64_t initial = HashWorld(simulation.Observe());
  const std::string initial_digest = CaptureSimulationDigest(simulation);
  Advance(simulation, 1000);
  const uint64_t advanced = HashWorld(simulation.Observe());
  const std::string advanced_digest = CaptureSimulationDigest(simulation);
  for (int repeat = 0; repeat < 2; ++repeat) {
    InitDefaultMatch(simulation);
    Require(&simulation.match()->GetAnimationLibrary() == animations,
            "reset replaced the Simulation-owned animation library");
    Require(HashWorld(simulation.Observe()) == initial &&
                CaptureSimulationDigest(simulation) == initial_digest,
            "reset changed initial authoritative state or RNG");
    Advance(simulation, 1000);
    Require(HashWorld(simulation.Observe()) == advanced &&
                CaptureSimulationDigest(simulation) == advanced_digest,
            "reset/replay changed authoritative state or RNG");
  }
  Require(simulation.Stop() && simulation.match() == nullptr &&
              !simulation.IsInPlay() && !simulation.Stop(),
          "direct Simulation stop is not idempotent");
  // Keep another library alive while rebuilding the stopped match. This also
  // catches a library accidentally released by Stop and reused by the allocator.
  Simulation independent;
  InitDefaultMatch(independent);
  Require(&independent.match()->GetAnimationLibrary() != animations,
          "independent Simulation inherited a released or ambient library");
  InitDefaultMatch(simulation);
  Require(&simulation.match()->GetAnimationLibrary() == animations &&
              HashWorld(simulation.Observe()) == initial &&
              CaptureSimulationDigest(simulation) == initial_digest,
          "Stop/Init lost the cached library, initial state or RNG replay");
}

void CheckImportHierarchy() {
  const char* data_dir = std::getenv("GFOOTBALL_DATA_DIR");
  Require(data_dir != nullptr, "missing offline import fixture directory");
  ImportLoader loader;
  ImportHierarchy hierarchy = loader.LoadObject(
      std::string(data_dir) + "/media/objects/players/player.object");
  Require(hierarchy.root && hierarchy.anchors.size() == 13,
          "offline player hierarchy changed");
  std::vector<Vector3> positions;
  for (const ImportNode* anchor : hierarchy.anchors) {
    positions.push_back(anchor->GetDerivedPosition());
  }
  hierarchy.root->UpdateDerivedTransforms();
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const Vector3 position = hierarchy.anchors[i]->GetDerivedPosition();
    Require(std::memcmp(position.coords, positions[i].coords,
                        sizeof(position.coords)) == 0,
            "offline transform cache changed float bits");
  }
}

void CheckModelComposition() {
  namespace model = football::model;
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  home.name = "Static Home";
  home.players.front().attributes.set(model::PlayerStat::physical_velocity,
                                     0.8123456f);
  const model::PlayerAttributes attributes = home.players.front().attributes;
  const model::Team declared_home = home;
  Simulation simulation;
  simulation.Init(home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  home.name = "Changed after initialization";
  home.players.front().attributes.fill(0.1f);
  for (int repeat = 0; repeat < 2; ++repeat) {
    Match* match = simulation.match();
    const football::model::Team& team = match->GetTeam(0)->GetModel();
    Require(team.name == "Static Home" &&
                team.players.at(0).attributes == attributes &&
                match->pitch() == model::MakeLegacyPitch(),
            "core lost owned team, ability or pitch descriptions");
    simulation.Stop();
    simulation.Init(declared_home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  }
}

// A/B branches are replayed from the same declared match, not restored through
// the deleted legacy checkpoint API. The hook remains a runtime test diagnostic.
void CheckMovementAnimationPerturbation(Simulation& simulation, bool frame_count) {
  struct Tick {
    std::string digest;
    int time_ms;
    int queries;
    bool perturbation_applied;
  };
  MovementAnimationPerturbation& hook = MovementAnimationPerturbationAudit();
  const auto run = [&](bool perturb) {
    hook = MovementAnimationPerturbation{};
    InitDefaultMatch(simulation);
    Advance(simulation, 600);
    const auto policy = football::test::MakeDefaultAI(simulation);
    hook.require_frame_count_difference = frame_count;
    std::vector<Tick> ticks;
    for (int tick = 0; tick < 4000; ++tick) {
      hook.enabled = perturb && tick >= 10;
      const int queries_before = PlayerDecisionClockQueries();
      football::test::StepDefaultAI(simulation, policy);
      CheckCanonicalFrame(simulation);
      ticks.push_back({CaptureSimulationDigest(simulation),
                       static_cast<int>(simulation.match()->GetActualTime_ms()),
                       PlayerDecisionClockQueries() - queries_before, hook.applied});
    }
    hook.enabled = false;
    return ticks;
  };
  const auto baseline = run(false);
  const auto changed = run(true);
  const MovementAnimationPerturbation event = hook;
  Require(event.applied && event.original_anim_id != event.alternative_anim_id,
          "animation perturbation did not change a Movement winner");
  const auto replay = run(false);
  int first_difference = -1;
  int event_tick = -1;
  for (std::size_t i = 0; i < baseline.size(); ++i) {
    Require(baseline[i].digest == replay[i].digest &&
                baseline[i].time_ms == replay[i].time_ms &&
                baseline[i].queries == replay[i].queries,
            "animation baseline is not deterministic under reset/replay");
    // Post-tick time can equal the next tick's pre-event time; use the hook
    // observed in this exact tick rather than infer an event from the clock.
    if (changed[i].perturbation_applied && event_tick < 0) {
      event_tick = static_cast<int>(i);
    }
    if (first_difference < 0 &&
        (baseline[i].digest != changed[i].digest ||
         baseline[i].time_ms != changed[i].time_ms ||
         baseline[i].queries != changed[i].queries)) {
      first_difference = static_cast<int>(i);
    }
  }
  Require(event_tick >= 10, "animation test lacks an unperturbed prefix");
  Require(first_difference < 0 || first_difference >= event_tick,
          "animation branches diverged before the perturbation event");
  if (frame_count) {
    Require(first_difference >= event_tick,
            "frame-count perturbation lost its observable lifecycle effect");
  }
  std::cout << "  core_animation_ab mode="
            << (frame_count ? "frame_count" : "foot_order")
            << " event_tick=" << event_tick
            << " first_difference=" << first_difference << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  install_stacktrace();
  std::cout.precision(17);
  std::cout << std::unitbuf;
  try {
    CheckPlayerKinematics();
    CheckPlayerKinematicMirror();
    CheckPlayerBodyFacing();
    CheckProceduralLocomotion();
    CheckProceduralLocomotionPrediction();
    CheckProceduralInterceptPrediction();
    CheckPlayerGroundCollider();
    CheckPlayerActionExecutor();
    CheckPlayerDecisionScheduler();
    CheckPureLocomotionBoundary();
    CheckLegacyLocomotionCommandAdapter();
    CheckPlayerActionVolume();
    CheckPlayerBodyCollider();
    CheckModelComposition();
    Simulation simulation;
    InitDefaultMatch(simulation);
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--animation-ab" || mode == "--animation-ab-lifecycle") {
      CheckMovementAnimationPerturbation(simulation, mode == "--animation-ab-lifecycle");
    } else {
      Require(mode.empty() || mode == "--print-baseline", "unknown regression mode");
      GameEnv game{football::app::fixtures::MakeDefaultHomeTeam(),
                   football::app::fixtures::MakeDefaultAwayTeam(),
                   football::model::MakeLegacyPitch(), {}, {}};
      game.Start();
      CheckGoldenSnapshots(simulation, game, mode == "--print-baseline");
      if (mode == "--print-baseline") return 0;
      CheckResetDeterminism(simulation);
      CheckRefereeRules(simulation);
      CheckFlattenedPlayerLifecycle(simulation);
      CheckImportHierarchy();
    }
    std::cout << "football_regression: PASS (core API)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_regression: FAIL: " << error.what() << '\n';
    return 1;
  }
}
