#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "env/game_env.hpp"
#include "sim/ai_support/AIfunctions.hpp"
#include "sim/player/legacy_locomotion_command.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_body_facing.hpp"
#include "sim/player/player_locomotion.hpp"
#include "sim/player/player_ground_collider.hpp"
#include "sim/player/player_action_executor.hpp"
#include "sim/player/player_action_volume.hpp"
#include "sim/player/player_body_collider.hpp"
#include "sim/player/player_retain_anchor.hpp"
#include "animation/animcollection.hpp"
#include "animation/import_hierarchy.hpp"
#include "animation/import_loader.hpp"
#include "sim/player/player_decision_scheduler.hpp"

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

void RequirePositionNear(const Position& actual, const Position& expected,
                         const std::string& label) {
  for (int axis = 0; axis < 3; ++axis) {
    RequireNear(actual.env_coord(axis), expected.env_coord(axis),
                label + "[" + std::to_string(axis) + "]");
  }
}

void RequireInfoEqual(const SharedInfo& actual, const SharedInfo& expected,
                      const std::string& label) {
  Require(actual.step == expected.step, label + ": step differs");
  Require(actual.left_goals == expected.left_goals,
          label + ": left score differs");
  Require(actual.right_goals == expected.right_goals,
          label + ": right score differs");
  Require(actual.game_mode == expected.game_mode, label + ": game mode differs");
  Require(actual.is_in_play == expected.is_in_play,
          label + ": in-play state differs");
  Require(actual.ball_owned_team == expected.ball_owned_team,
          label + ": ball owner team differs");
  Require(actual.ball_owned_player == expected.ball_owned_player,
          label + ": ball owner player differs");
  RequirePositionNear(actual.ball_position, expected.ball_position,
                      label + ": ball position");
  RequirePositionNear(actual.ball_direction, expected.ball_direction,
                      label + ": ball direction");
  RequirePositionNear(actual.ball_rotation, expected.ball_rotation,
                      label + ": ball rotation");

  const auto compare_players = [&](const std::vector<PlayerInfo>& current,
                                   const std::vector<PlayerInfo>& reference,
                                   const char* team_name) {
    Require(current.size() == reference.size(),
            label + ": " + team_name + " player count differs");
    for (size_t index = 0; index < current.size(); ++index) {
      const PlayerInfo& player = current[index];
      const PlayerInfo& expected_player = reference[index];
      const std::string player_label = label + ": " + team_name +
          " player " + std::to_string(index);
      RequirePositionNear(player.player_position, expected_player.player_position,
                          player_label + " position");
      RequirePositionNear(player.player_direction, expected_player.player_direction,
                          player_label + " direction");
      RequireNear(player.tired_factor, expected_player.tired_factor,
                  player_label + " tired factor");
      Require(player.has_card == expected_player.has_card,
              player_label + " card differs");
      Require(player.is_active == expected_player.is_active,
              player_label + " active state differs");
      Require(player.designated_player == expected_player.designated_player,
              player_label + " designated state differs");
      Require(player.role == expected_player.role, player_label + " role differs");
    }
  };

  compare_players(actual.left_team, expected.left_team, "left");
  compare_players(actual.right_team, expected.right_team, "right");
  Require(actual.left_controllers == expected.left_controllers,
          label + ": left controllers differ");
  Require(actual.right_controllers == expected.right_controllers,
          label + ": right controllers differ");
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

uint64_t HashPosition(uint64_t hash, const Position& position) {
  for (int axis = 0; axis < 3; ++axis) {
    const float value = position.env_coord(axis);
    hash = HashValue(hash, value);
  }
  return hash;
}

uint64_t HashInfo(const SharedInfo& info) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash = HashPosition(hash, info.ball_position);
  hash = HashPosition(hash, info.ball_direction);
  hash = HashPosition(hash, info.ball_rotation);
  hash = HashValue(hash, info.left_goals);
  hash = HashValue(hash, info.right_goals);
  hash = HashValue(hash, info.game_mode);
  hash = HashValue(hash, info.is_in_play);
  hash = HashValue(hash, info.ball_owned_team);
  hash = HashValue(hash, info.ball_owned_player);
  hash = HashValue(hash, info.step);

  const auto hash_players = [&](const std::vector<PlayerInfo>& players) {
    uint64_t result = HashValue(hash, players.size());
    for (const PlayerInfo& player : players) {
      result = HashPosition(result, player.player_position);
      result = HashPosition(result, player.player_direction);
      result = HashValue(result, player.has_card);
      result = HashValue(result, player.is_active);
      result = HashValue(result, player.designated_player);
      result = HashValue(result, player.tired_factor);
      result = HashValue(result, player.role);
    }
    return result;
  };
  hash = hash_players(info.left_team);
  hash = hash_players(info.right_team);

  const auto hash_controllers = [&](const std::vector<ControllerInfo>& controllers) {
    uint64_t result = HashValue(hash, controllers.size());
    for (const ControllerInfo& controller : controllers) {
      result = HashValue(result, controller.controlled_player);
    }
    return result;
  };
  hash = hash_controllers(info.left_controllers);
  hash = hash_controllers(info.right_controllers);
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
std::string CaptureSimulationDigest(GameEnv& env) {
  std::string out;
  Match* match = env.context->gameTask->GetMatch();

  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  std::vector<PlayerBase*> actors(players.begin(), players.end());
  std::vector<PlayerBase*> officials;
  match->GetOfficialPlayers(officials);
  actors.insert(actors.end(), officials.begin(), officials.end());

  AppendDigestInt(out, actors.size());
  for (const PlayerBase* actor : actors) {
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

  // Simulation RNG state. This is part of simulation state, so it is also what
  // get_state/set_state saves; the draw count is diagnostics only and is
  // therefore reported separately rather than folded into the digest.
  std::ostringstream rngState;
  rngState << env.context->rng.engine();
  out += rngState.str();

  return out;
}

// The semantic digest is the reset contract. These named float fields are
// only diagnostics: they map a raw mismatch back to an authoritative actor
// field without weakening the byte-for-byte comparison. Do not compare the
// EnvState blob itself here: it serializes POD padding (including radian),
// which is transport representation rather than simulation state.
struct NamedDigestFloat {
  std::string name;
  uint32_t bits;
};

uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void AppendNamedDigestFloat(std::vector<NamedDigestFloat>& fields,
                            const std::string& name, float value) {
  fields.push_back({name, FloatBits(value)});
}

std::vector<NamedDigestFloat> CaptureResetDigestFloats(GameEnv& env) {
  std::vector<NamedDigestFloat> fields;
  Match* match = env.context->gameTask->GetMatch();

  const auto append_actor = [&](const PlayerBase* actor,
                                const std::string& actor_name) {
    const PlayerKinematicState& kinematics = actor->GetKinematicState();
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.position.x",
                           kinematics.position.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.position.y",
                           kinematics.position.coords[1]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.velocity.x",
                           kinematics.velocity.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.velocity.y",
                           kinematics.velocity.coords[1]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.facing.x",
                           kinematics.facing.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.facing.y",
                           kinematics.facing.coords[1]);
    const PlayerGroundCollider& ground = actor->GetGroundCollider();
    AppendNamedDigestFloat(fields, actor_name + ".ground.center.x",
                           ground.center.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".ground.center.y",
                           ground.center.coords[1]);
    AppendNamedDigestFloat(fields, actor_name + ".ground.radius",
                           ground.radius);
    const PlayerActionState& action = actor->GetSimulationActionState();
    AppendNamedDigestFloat(fields, actor_name + ".action.contactPosition.x",
                           action.contactPosition.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".action.contactPosition.y",
                           action.contactPosition.coords[1]);
  };

  for (int team_id : {match->FirstTeam(), match->SecondTeam()}) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(team_id, players);
    for (size_t index = 0; index < players.size(); ++index) {
      append_actor(players[index], "team[" + std::to_string(team_id) +
                                     "].player[" + std::to_string(index) + "]");
    }
  }
  std::vector<PlayerBase*> officials;
  match->GetOfficialPlayers(officials);
  for (size_t index = 0; index < officials.size(); ++index) {
    append_actor(officials[index], "official[" + std::to_string(index) + "]");
  }

  Ball* ball = match->GetBall();
  const Vector3 ball_position = ball->Predict(0);
  const Vector3 ball_momentum = ball->GetMovement();
  const Vector3 ball_rotation = ball->GetRotation();
  for (int axis = 0; axis < 3; ++axis) {
    const char axis_name[] = {'x', 'y', 'z'};
    AppendNamedDigestFloat(fields, std::string("ball.position.") + axis_name[axis],
                           ball_position.coords[axis]);
    AppendNamedDigestFloat(fields, std::string("ball.momentum.") + axis_name[axis],
                           ball_momentum.coords[axis]);
    AppendNamedDigestFloat(fields, std::string("ball.rotation.") + axis_name[axis],
                           ball_rotation.coords[axis]);
  }
  const RefereeBuffer& buffer = match->GetReferee()->GetBuffer();
  AppendNamedDigestFloat(fields, "referee.restartPosition.x",
                         buffer.restartPos.coords[0]);
  AppendNamedDigestFloat(fields, "referee.restartPosition.y",
                         buffer.restartPos.coords[1]);
  return fields;
}

std::string DescribeFirstResetDigestDifference(
    const std::vector<NamedDigestFloat>& first,
    const std::vector<NamedDigestFloat>& second) {
  if (first.size() != second.size()) {
    return "reset diagnostic field count differs";
  }
  for (size_t index = 0; index < first.size(); ++index) {
    if (first[index].bits == second[index].bits) continue;
    std::ostringstream message;
    message << first[index].name << ": reset #1 bits=0x" << std::hex
            << first[index].bits << ", reset #2 bits=0x" << second[index].bits;
    return message.str();
  }
  return "named float fields match; inspect non-float/raw digest state";
}



// Invariant #7: a presentation operation must not modify any state the next
// simulation tick reads.

ScenarioConfig MakeBuiltinAiConfig() {
  auto config = ScenarioConfig::make();
  config->left_agents = 0;
  config->right_agents = 0;
  config->real_time = false;
  config->deterministic = true;
  config->game_engine_random_seed = 42;
  return *config;
}

void Advance(GameEnv& env, int steps) {
  for (int step = 0; step < steps; ++step) env.step();
}

void WaitUntilInPlay(GameEnv& env, int max_steps, const std::string& label) {
  for (int step = 0; step < max_steps; ++step) {
    if (env.context->gameTask->GetMatch()->IsInPlay()) return;
    env.step();
  }
  Require(env.context->gameTask->GetMatch()->IsInPlay(),
          label + ": game did not resume");
}

void CheckKinematicMirrorConsistency(GameEnv& env, const std::string& label) {
  Match* match = env.context->gameTask->GetMatch();
  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  std::vector<PlayerBase*> officials;
  match->GetOfficialPlayers(officials);
  Require(!players.empty(), label + ": missing active players");
  Require(!officials.empty(), label + ": missing officials");
  for (const Player* player : players) {
    Require(player->IsKinematicMirrorConsistent(),
            label + ": player kinematic mirror is stale");
  }
  for (const PlayerBase* official : officials) {
    Require(official->IsKinematicMirrorConsistent(),
            label + ": official kinematic mirror is stale");
  }
}

void CheckGoldenSnapshots(GameEnv& env, ScenarioConfig& config) {
  // Values below are a fixed-seed baseline. The hash covers every field in
  // SharedInfo; the explicit values make failures immediately diagnosable.
  struct GoldenSnapshot {
    int calls;
    int step;
    Position ball;
    Position left_player_0;
    Position right_player_0;
    int left_goals;
    int right_goals;
    bool is_in_play;
    uint64_t hash;
  };

  const GoldenSnapshot golden[] = {
      {1, -1, Position(0.0f, 0.0f, 0.110616393f, true),
       Position(-1.01102936f, 0.0f, 0.0f, true),
       Position(1.01102936f, 0.0f, 0.0f, true), 0, 0, false,
       UINT64_C(385886754253041681)},
      // 4f-a3b3: locomotion cadence samples cached Movement without triggering
      // SelectAnim; animation opportunities alone own presentation selection.
      {100, 79, Position(0.582221329f, 0.16972594f, 0.117803708f, true),
       Position(-0.825538993f, 0.00424938463f, 0.0f, true),
       Position(0.98832047f, 0.00904292241f, 0.0f, true), 0, 0, true,
       UINT64_C(10610695401738187593)},
      {500, 437, Position(-0.0636564866f, -0.0195256099f, 0.698153377f, true),
       Position(-0.986044109f, -1.55089154e-07f, 0.0f, true),
       Position(0.851519883f, -3.59507176e-05f, 0.0f, true), 0, 0, true,
       UINT64_C(2707498601741816980)},
      {1000, 916, Position(0.925300062f, -0.14447403f, 0.120126799f, true),
       Position(-0.816613317f, 0.00411171326f, 0.0f, true),
       Position(0.957633913f, -0.0314540565f, 0.0f, true), 0, 0, true,
       UINT64_C(4581787733472518770)},
  };



  env.reset(config, false);
  int completed = 0;
  for (const GoldenSnapshot& expected : golden) {
    Advance(env, expected.calls - completed);
    completed = expected.calls;
    const SharedInfo info = env.get_info();
    const std::string label = "snapshot at call " + std::to_string(expected.calls);
    Require(info.step == expected.step, label + ": step differs");
    Require(info.left_goals == expected.left_goals &&
                info.right_goals == expected.right_goals,
            label + ": score differs");
    Require(info.is_in_play == expected.is_in_play, label + ": in-play differs");
    Require(!info.left_team.empty() && !info.right_team.empty(),
            label + ": missing active players");
    RequirePositionNear(info.ball_position, expected.ball, label + ": ball");
    RequirePositionNear(info.left_team.front().player_position,
                        expected.left_player_0, label + ": left player 0");
    RequirePositionNear(info.right_team.front().player_position,
                        expected.right_player_0, label + ": right player 0");
    CheckKinematicMirrorConsistency(env, label);
    const uint64_t hash = HashInfo(info);
    if (hash != expected.hash) {
      std::ostringstream message;
      message << label << ": expected hash " << expected.hash << ", got "
              << hash;
      throw RegressionFailure(message.str());
    }
  }
}

void CheckActionStateOracle(Match *match, const std::string &label) {
  for (int teamID = 0; teamID < 2; ++teamID) {
    std::vector<Player *> players;
    match->GetTeam(teamID)->GetActivePlayers(players);
    Require(!players.empty(), label + ": missing active players");
    for (const Player *player : players) {
      player->CheckSimulationActionOracle();
      const PlayerActionState &simulation =
          player->GetSimulationActionState();
      Require(player->GetCurrentFunctionType() == simulation.type,
              label + ": gameplay action type differs");
      Require(player->GetCurrentFrame() == simulation.frame,
              label + ": gameplay action frame differs");
      Require(player->GetTouchFrame() == simulation.contactFrame,
              label + ": gameplay contact frame differs");
      Require(player->TouchAnim() == simulation.HasScheduledContact(),
              label + ": gameplay contact schedule differs");
      Require(player->TouchPending() == simulation.IsContactPending(),
              label + ": gameplay pending-contact differs");
      for (int axis = 0; axis < 3; ++axis) {
        RequireNear(player->GetTouchPos().coords[axis],
                    simulation.contactPosition.coords[axis],
                    label + ": gameplay contact position");
      }
    }
  }
}

void CheckResetBitDeterminism(GameEnv& env, const ScenarioConfig& config) {
  const Vector3 caller_ball_position = config.ball_position;
  const auto require_caller_config_unchanged = [&]() {
    for (int axis = 0; axis < 3; ++axis) {
      Require(FloatBits(config.ball_position.coords[axis]) ==
                  FloatBits(caller_ball_position.coords[axis]),
              "reset at tick 0: reset mutated caller ball_position[" +
                  std::to_string(axis) + "]");
    }
  };

  env.reset(config, false);
  require_caller_config_unchanged();
  const std::string first_digest = CaptureSimulationDigest(env);
  const std::vector<NamedDigestFloat> first_fields =
      CaptureResetDigestFloats(env);

  env.reset(config, false);
  require_caller_config_unchanged();
  const std::string second_digest = CaptureSimulationDigest(env);
  const std::vector<NamedDigestFloat> second_fields =
      CaptureResetDigestFloats(env);

  Require(first_digest == second_digest,
          "reset at tick 0: simulation digest differs: " +
              DescribeFirstResetDigestDifference(first_fields, second_fields));
}

// H3e0c: the movement authority must be same-tick observable. A position
// correction (the primitive Match::CheckHumanoidCollisions uses) has to be
// visible immediately through every view of the actor, not committed at the
// end of the tick. This is the movement equivalent of the Ball::Touch
// same-tick visibility that H3d1 uncovered.
void CheckMovementAuthorityTiming(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "movement authority timing: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  Require(players.size() >= 2,
          "movement authority timing: missing active players");

  // A collision correction is immediately observable through the Player
  // kinematic state, the ground collider and the Humanoid spatial state.
  Player* corrected = players[0];
  const Vector3 before = corrected->GetPosition();
  const Vector3 offset(1.25f, -0.5f, 0.0f);
  corrected->OffsetPosition(offset);
  const Vector3 expected = before + offset;
  for (int axis = 0; axis < 3; ++axis) {
    Require(FloatBits(corrected->GetPosition().coords[axis]) ==
                FloatBits(expected.coords[axis]),
            "movement authority: OffsetPosition is not immediately visible "
            "through the Player kinematic state");
  }
  for (int axis = 0; axis < 2; ++axis) {
    Require(FloatBits(corrected->GetGroundCollider().center.coords[axis]) ==
                FloatBits(expected.coords[axis]),
            "movement authority: OffsetPosition left the collider behind");
  }
  Require(corrected->IsKinematicMirrorConsistent(),
          "movement authority: OffsetPosition left the kinematic mirror "
          "stale");

  // Corrections compose in call order: a later correction must observe the
  // position produced by the earlier one within the same tick.
  Player* chained = players[2];
  const Vector3 chain_start = chained->GetPosition();
  const Vector3 first_offset(0.5f, 0.25f, 0.0f);
  const Vector3 second_offset(-0.2f, 0.75f, 0.0f);
  chained->OffsetPosition(first_offset);
  chained->OffsetPosition(second_offset);
  const Vector3 chain_expected = chain_start + first_offset + second_offset;
  for (int axis = 0; axis < 3; ++axis) {
    Require(FloatBits(chained->GetPosition().coords[axis]) ==
                FloatBits(chain_expected.coords[axis]),
            "movement authority: chained corrections did not observe each "
            "other in call order");
  }
  Require(chained->IsKinematicMirrorConsistent(),
          "movement authority: chained corrections left the kinematic "
          "mirror stale");

  // Force an overlap, then run a real tick. Match::CheckHumanoidCollisions()
  // corrects positions after the actor ticks; the fatal movement oracle runs
  // inside that path, so a stale mirror there aborts the run.
  Player* p1 = players[0];
  Player* p2 = players[1];
  const Vector3 p1_position = p1->GetPosition();
  p2->OffsetPosition(p1_position + Vector3(0.15f, 0.0f, 0.0f) -
                     p2->GetPosition());
  Require(p1->GetGroundCollider().Intersects(p2->GetGroundCollider()),
          "movement authority: could not force the colliders to overlap");
  env.step();
  CheckKinematicMirrorConsistency(
      env, "kinematic mirror after an overlapped collision tick");
}

// H3e1a: measure how far the existing procedural locomotion model
// (PlayerKinematics) is from the legacy animation root motion over one tick of
// pure locomotion. This is deliberately a measurement, not a contract: it
// quantifies the semantic change H3e1b would accept, so the numbers are
// reported instead of asserted. Nothing in the simulation reads this.
void MeasureProceduralLocomotionDivergence(GameEnv& env,
                                           ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "procedural locomotion divergence: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  struct PendingSample {
    Player* player;
    PlayerKinematicState state;
    PlayerCommand command;
    int anim_id;
  };

  // Raw counts everything the boundary covers, including ticks that were not
  // a continuation of the sampled action (a requeue selects a new command
  // inside the tick) and ticks whose position was teleported or collision
  // corrected. Those are not model error, so the clean subset below is what
  // actually characterises the procedural model's per-tick gap.
  int samples = 0;
  int clean_samples = 0;
  int eligible_observations = 0;
  double clean_position_error_sum = 0.0;
  double clean_velocity_error_sum = 0.0;
  double clean_facing_error_sum = 0.0;
  double clean_max_position_error = 0.0;
  double clean_legacy_step_sum = 0.0;
  const double plausible_step_limit = 0.15;

  // H3e1b calibration. A procedural model cannot reproduce animation root
  // motion tick by tick, so it has to be designed against aggregate legacy
  // behaviour instead: the mapping from commanded speed to actual speed, the
  // achievable turn per tick as a function of facing error, and the speed
  // change achievable per tick as a function of the speed gap.
  const int desired_bucket_count = 9;  // 1 m/s bins
  int desired_count[9] = {0};
  double desired_sum_commanded[9] = {0.0};
  double desired_sum_legacy_speed[9] = {0.0};
  const int turn_bucket_count = 8;  // 0.25 rad bins
  int turn_count[8] = {0};
  double turn_sum_legacy[8] = {0.0};
  double turn_sum_procedural[8] = {0.0};
  const int gap_bucket_count = 9;  // 1 m/s bins over -4 .. +5
  int gap_count[9] = {0};
  double gap_sum_speed_change[9] = {0.0};
  double commanded_ratio_sum = 0.0;
  int commanded_ratio_samples = 0;
  double locomotion_position_error_sum = 0.0;
  double locomotion_velocity_error_sum = 0.0;
  double locomotion_facing_error_sum = 0.0;
  double locomotion_max_position_error = 0.0;
  int legacy_facing_velocity_samples = 0;
  double legacy_facing_velocity_error_sum = 0.0;
  int movement_facing_samples = 0;
  double movement_facing_error_sum = 0.0;

  const int ticks = 400;
  for (int tick = 0; tick < ticks; ++tick) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(match->FirstTeam(), players);
    match->GetActiveTeamPlayers(match->SecondTeam(), players);

    std::vector<PendingSample> pending;
    for (Player* player : players) {
      if (player->IsEligibleForProceduralLocomotion()) {
        ++eligible_observations;
      }
      const Anim* anim = player->GetCurrentAnim();
      const PlayerCommand& command = anim->originatingCommand;
      if (!player->IsEligibleForProceduralLocomotion() ||
          !command.useDesiredMovement) {
        continue;
      }
      pending.push_back(PendingSample{player, player->GetKinematicState(),
                                      command, anim->id});
    }

    env.step();

    for (const PendingSample& sample : pending) {
      if (!sample.player->IsEligibleForProceduralLocomotion()) continue;
      PlayerKinematicState predicted = sample.state;
      PlayerKinematicInput input;
      const float desiredSpeed =
          clamp(sample.command.desiredVelocityFloat, 0.0f,
                sample.player->GetMaxVelocity());
      input.desiredVelocity =
          sample.command.desiredDirection.Get2D().GetNormalized(
              predicted.facing) *
          desiredSpeed;
      input.desiredFacing =
          input.desiredVelocity.GetNormalized(predicted.facing);
      if (sample.command.useDesiredLookAt) {
        input.desiredFacing =
            (sample.command.desiredLookAt - predicted.position)
                .Get2D()
                .GetNormalized(input.desiredFacing);
      }
      PlayerKinematicParameters parameters;
      parameters.maxSpeed = sample.player->GetMaxVelocity();
      PlayerKinematics::Step(predicted, input, parameters, 0.01f);
      const PlayerKinematicState& actual =
          sample.player->GetKinematicState();
      // The new procedural locomotion model, evaluated on exactly the same
      // sample so the two models are directly comparable.
      PlayerKinematicState predicted_locomotion = sample.state;
      const PlayerLocomotionInput locomotion_input =
          BuildLegacyLocomotionInput(sample.command, sample.state,
                                     sample.player->GetMaxVelocity(),
                                     sample.player->GetBodyDirectionVec());
      PlayerLocomotionParameters locomotion_parameters;
      locomotion_parameters.maxSpeed = sample.player->GetMaxVelocity();
      PlayerLocomotion::Step(predicted_locomotion, locomotion_input,
                             locomotion_parameters, 0.01f);
      const double locomotion_position_error =
          (predicted_locomotion.position - actual.position).GetLength();
      const double locomotion_velocity_error =
          (predicted_locomotion.velocity - actual.velocity).GetLength();
      const double locomotion_facing_error = std::fabs(
          predicted_locomotion.facing.GetAngle2D(actual.facing));
      const double position_error =
          (predicted.position - actual.position).GetLength();
      const double velocity_error =
          (predicted.velocity - actual.velocity).GetLength();
      const double facing_error =
          std::fabs(predicted.facing.GetAngle2D(actual.facing));
      ++samples;

      const double legacy_step =
          (actual.position - sample.state.position).GetLength();
      const bool action_continued =
          sample.player->GetCurrentAnim()->id == sample.anim_id;
      if (action_continued && legacy_step <= plausible_step_limit) {
        clean_position_error_sum += position_error;
        clean_velocity_error_sum += velocity_error;
        clean_facing_error_sum += facing_error;
        clean_legacy_step_sum += legacy_step;
        if (position_error > clean_max_position_error) {
          clean_max_position_error = position_error;
        }
        locomotion_position_error_sum += locomotion_position_error;
        locomotion_velocity_error_sum += locomotion_velocity_error;
        locomotion_facing_error_sum += locomotion_facing_error;
        if (locomotion_position_error > locomotion_max_position_error) {
          locomotion_max_position_error = locomotion_position_error;
        }
        ++clean_samples;

        const double current_speed = sample.state.velocity.GetLength();
        const double legacy_speed = actual.velocity.GetLength();
        const int desired_bucket = clamp(
            static_cast<int>(std::floor(desiredSpeed)), 0,
            desired_bucket_count - 1);
        ++desired_count[desired_bucket];
        desired_sum_commanded[desired_bucket] += desiredSpeed;
        desired_sum_legacy_speed[desired_bucket] += legacy_speed;
        if (desiredSpeed > 0.5f) {
          commanded_ratio_sum += legacy_speed / desiredSpeed;
          ++commanded_ratio_samples;
        }

        const double turn_error = std::fabs(
            sample.state.facing.GetAngle2D(input.desiredFacing));
        const int turn_bucket = clamp(
            static_cast<int>(turn_error / 0.25), 0, turn_bucket_count - 1);
        ++turn_count[turn_bucket];
        turn_sum_legacy[turn_bucket] += std::fabs(
            sample.state.facing.GetAngle2D(actual.facing));
        turn_sum_procedural[turn_bucket] += std::fabs(
            sample.state.facing.GetAngle2D(predicted.facing));

        const double speed_gap = desiredSpeed - current_speed;
        const int gap_bucket = clamp(
            static_cast<int>(std::floor(speed_gap + 4.0)), 0,
            gap_bucket_count - 1);
        ++gap_count[gap_bucket];
        gap_sum_speed_change[gap_bucket] += legacy_speed - current_speed;

        // Structural check: legacy facing is the movement direction while
        // moving, not a separately turned look direction. If that holds, a
        // procedural model should derive facing from velocity instead of
        // integrating a turn rate.
        // Mirror the exact legacy condition: directionVec equals the movement
        // direction only when the quantized velocity is not idle.
        const bool moving =
            sample.player->GetEnumVelocity() != e_Velocity_Idle;
        if (moving) {
          legacy_facing_velocity_error_sum += std::fabs(
              actual.facing.GetAngle2D(
                  actual.velocity.Get2D().GetNormalized(actual.facing)));
          ++legacy_facing_velocity_samples;
        }
        if (moving) {
          movement_facing_error_sum += std::fabs(
              actual.facing.GetAngle2D(
                  predicted.velocity.Get2D().GetNormalized(actual.facing)));
          ++movement_facing_samples;
        }
      }

      Require(predicted.position.coords[2] == 0.0f,
              "procedural locomotion: a planar prediction left the pitch "
              "plane");
      Require(predicted.velocity.coords[2] == 0.0f,
              "procedural locomotion: a planar prediction produced vertical "
              "velocity");
    }
  }

  Require(eligible_observations > 0,
          "procedural locomotion: the eligibility boundary never held, so "
          "the divergence measurement would be vacuous");
  Require(samples > 0,
          "procedural locomotion: no comparable locomotion tick was "
          "collected");
  Require(clean_samples > 0,
          "procedural locomotion: no continued-action locomotion tick was "
          "collected");
  std::cout << "locomotion divergence: eligible_observations="
            << eligible_observations << " samples=" << samples
            << " clean_samples=" << clean_samples
            << " clean_mean_position_error="
            << (clean_position_error_sum / clean_samples)
            << " clean_max_position_error=" << clean_max_position_error
            << " clean_mean_velocity_error="
            << (clean_velocity_error_sum / clean_samples)
            << " clean_mean_facing_error="
            << (clean_facing_error_sum / clean_samples)
            << " clean_mean_legacy_step="
            << (clean_legacy_step_sum / clean_samples) << "\n";
  std::cout << "locomotion divergence (new model): clean_samples="
            << clean_samples << " mean_position_error="
            << (locomotion_position_error_sum / clean_samples)
            << " max_position_error=" << locomotion_max_position_error
            << " mean_velocity_error="
            << (locomotion_velocity_error_sum / clean_samples)
            << " mean_facing_error="
            << (locomotion_facing_error_sum / clean_samples) << "\n";
  std::cout << "locomotion facing semantics: legacy_facing_vs_velocity_error="
            << (legacy_facing_velocity_samples > 0
                    ? legacy_facing_velocity_error_sum /
                          legacy_facing_velocity_samples
                    : 0.0)
            << " samples=" << legacy_facing_velocity_samples
            << " movement_facing_vs_legacy_error="
            << (movement_facing_samples > 0
                    ? movement_facing_error_sum / movement_facing_samples
                    : 0.0)
            << " samples=" << movement_facing_samples << "\n";
  std::cout << "locomotion calibration: commanded_ratio="
            << (commanded_ratio_samples > 0
                    ? commanded_ratio_sum / commanded_ratio_samples
                    : 0.0)
            << " commanded_ratio_samples=" << commanded_ratio_samples << "\n";
  for (int bucket = 0; bucket < desired_bucket_count; ++bucket) {
    if (desired_count[bucket] == 0) continue;
    std::cout << "  desired_speed_bucket[" << bucket << "] count="
              << desired_count[bucket] << " mean_commanded="
              << (desired_sum_commanded[bucket] / desired_count[bucket])
              << " mean_legacy_speed="
              << (desired_sum_legacy_speed[bucket] / desired_count[bucket])
              << "\n";
  }
  for (int bucket = 0; bucket < turn_bucket_count; ++bucket) {
    if (turn_count[bucket] == 0) continue;
    std::cout << "  facing_error_bucket[" << bucket << "] count="
              << turn_count[bucket] << " mean_legacy_turn="
              << (turn_sum_legacy[bucket] / turn_count[bucket])
              << " mean_procedural_turn="
              << (turn_sum_procedural[bucket] / turn_count[bucket]) << "\n";
  }
  for (int bucket = 0; bucket < gap_bucket_count; ++bucket) {
    if (gap_count[bucket] == 0) continue;
    std::cout << "  speed_gap_bucket[" << (bucket - 4) << "] count="
              << gap_count[bucket] << " mean_legacy_speed_change="
              << (gap_sum_speed_change[bucket] / gap_count[bucket]) << "\n";
  }
}

const char* VelocityClassName(e_Velocity velocity) {
  switch (velocity) {
    case e_Velocity_Idle:
      return "idle";
    case e_Velocity_Dribble:
      return "dribble";
    case e_Velocity_Walk:
      return "walk";
    case e_Velocity_Sprint:
      return "sprint";
  }
  return "unknown";
}

// H3e1b-prep2: two narrow questions, deliberately nothing else.
//
// 1. Velocity class is an animation-selection regime, not a physical value:
//    actual movement stays continuous. Does the current-class to
//    desired-class transition tell us something a single speed-gap bucket
//    cannot?
// 2. Is a large direction error handled by rotating the velocity, or by
//    braking it and building a new direction? Those are different models, so
//    they must be separated before any procedural model is designed.
void MeasureLocomotionRegimeTransitions(GameEnv& env,
                                        ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "locomotion regimes: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  struct TransitionCell {
    int samples = 0;
    double sum_current_speed = 0.0;
    double sum_next_speed = 0.0;
  };
  TransitionCell transitions[4][4];

  const int direction_bucket_count = 4;
  const double direction_bucket_limits[4] = {0.25, 0.75, 1.5, 3.2};
  int direction_count[4] = {0};
  double direction_sum_current_speed[4] = {0.0};
  double direction_sum_next_speed[4] = {0.0};
  double direction_sum_velocity_turn[4] = {0.0};
  double direction_sum_desired_speed[4] = {0.0};
  const double plausible_step_limit = 0.15;

  struct PendingSample {
    Player* player;
    PlayerKinematicState state;
    PlayerCommand command;
    int anim_id;
  };

  const int ticks = 400;
  for (int tick = 0; tick < ticks; ++tick) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(match->FirstTeam(), players);
    match->GetActiveTeamPlayers(match->SecondTeam(), players);

    std::vector<PendingSample> pending;
    for (Player* player : players) {
      const Anim* anim = player->GetCurrentAnim();
      const PlayerCommand& command = anim->originatingCommand;
      if (!player->IsEligibleForProceduralLocomotion() ||
          !command.useDesiredMovement) {
        continue;
      }
      pending.push_back(PendingSample{player, player->GetKinematicState(),
                                      command, anim->id});
    }

    env.step();

    for (const PendingSample& sample : pending) {
      if (!sample.player->IsEligibleForProceduralLocomotion()) continue;
      const PlayerKinematicState& actual =
          sample.player->GetKinematicState();
      const double legacy_step =
          (actual.position - sample.state.position).GetLength();
      if (sample.player->GetCurrentAnim()->id != sample.anim_id ||
          legacy_step > plausible_step_limit) {
        continue;
      }

      const double current_speed = sample.state.velocity.GetLength();
      const double next_speed = actual.velocity.GetLength();
      const e_Velocity current_class =
          FloatToEnumVelocity(static_cast<float>(current_speed));
      const e_Velocity desired_class = FloatToEnumVelocity(clamp(
          sample.command.desiredVelocityFloat, 0.0f, sprintVelocity));
      TransitionCell& cell =
          transitions[static_cast<int>(current_class)]
                     [static_cast<int>(desired_class)];
      ++cell.samples;
      cell.sum_current_speed += current_speed;
      cell.sum_next_speed += next_speed;

      if (current_speed > 0.5) {
        const Vector3 current_direction =
            sample.state.velocity.Get2D().GetNormalized(sample.state.facing);
        const Vector3 desired_direction =
            sample.command.desiredDirection.Get2D().GetNormalized(
                current_direction);
        const double direction_error = std::fabs(
            current_direction.GetAngle2D(desired_direction));
        const double velocity_turn = std::fabs(current_direction.GetAngle2D(
            actual.velocity.Get2D().GetNormalized(current_direction)));
        int bucket = direction_bucket_count - 1;
        for (int i = 0; i < direction_bucket_count; ++i) {
          if (direction_error < direction_bucket_limits[i]) {
            bucket = i;
            break;
          }
        }
        ++direction_count[bucket];
        direction_sum_current_speed[bucket] += current_speed;
        direction_sum_next_speed[bucket] += next_speed;
        direction_sum_velocity_turn[bucket] += velocity_turn;
        direction_sum_desired_speed[bucket] += clamp(
            sample.command.desiredVelocityFloat, 0.0f, sprintVelocity);
      }
    }
  }

  std::cout << "locomotion regime transitions (current -> desired):\n";
  for (int from = 0; from < 4; ++from) {
    for (int to = 0; to < 4; ++to) {
      const TransitionCell& cell = transitions[from][to];
      if (cell.samples == 0) continue;
      std::cout << "  " << VelocityClassName(static_cast<e_Velocity>(from))
                << " -> " << VelocityClassName(static_cast<e_Velocity>(to))
                << " samples=" << cell.samples << " mean_current_speed="
                << (cell.sum_current_speed / cell.samples)
                << " mean_next_speed="
                << (cell.sum_next_speed / cell.samples) << "\n";
    }
  }

  std::cout << "locomotion direction error response:\n";
  for (int bucket = 0; bucket < direction_bucket_count; ++bucket) {
    if (direction_count[bucket] == 0) continue;
    std::cout << "  direction_error_lt_" << direction_bucket_limits[bucket]
              << " count=" << direction_count[bucket]
              << " mean_desired_speed="
              << (direction_sum_desired_speed[bucket] / direction_count[bucket])
              << " mean_current_speed="
              << (direction_sum_current_speed[bucket] / direction_count[bucket])
              << " mean_next_speed="
              << (direction_sum_next_speed[bucket] / direction_count[bucket])
              << " mean_velocity_turn="
              << (direction_sum_velocity_turn[bucket] /
                  direction_count[bucket])
              << "\n";
  }

  int total = 0;
  for (int from = 0; from < 4; ++from) {
    for (int to = 0; to < 4; ++to) total += transitions[from][to].samples;
  }
  Require(total > 0,
          "locomotion regimes: no clean sample was collected, the "
          "measurement would be vacuous");
}

// H3e3b-prep1: establish the animation body-pose contract from the actual
// pure-locomotion trajectory before choosing any procedural body-facing
// parameters. This is measurement only: no production state reads these values.
void MeasureLegacyBodyFacing(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "body-facing measurement: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  struct PendingSample {
    Player* player;
    PlayerKinematicState state;
    PlayerCommand command;
    int anim_id;
  };
  struct Bucket {
    int samples = 0;
    int look_samples = 0;
    int turn_samples = 0;
    int continued_turn_samples = 0;
    double speed_sum = 0.0;
    std::vector<double> absolute_relative_angles;
    std::vector<double> absolute_desired_relative_angles;
    std::vector<double> absolute_look_errors;
    std::vector<double> absolute_turns;
    std::vector<double> absolute_continued_turns;
  };
  Bucket buckets[4];

  const int ticks = 400;
  for (int tick = 0; tick < ticks; ++tick) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(match->FirstTeam(), players);
    match->GetActiveTeamPlayers(match->SecondTeam(), players);

    std::vector<PendingSample> pending;
    for (Player* player : players) {
      if (!player->IsEligibleForProceduralLocomotion()) continue;
      const Anim* anim = player->GetCurrentAnim();
      pending.push_back(PendingSample{player, player->GetKinematicState(),
                                      anim->originatingCommand, anim->id});
    }

    env.step();

    for (const PendingSample& sample : pending) {
      const e_Velocity velocity_class =
          FloatToEnumVelocity(sample.state.velocity.GetLength());
      Bucket& bucket = buckets[static_cast<int>(velocity_class)];
      ++bucket.samples;
      bucket.speed_sum += sample.state.velocity.GetLength();
      bucket.absolute_relative_angles.push_back(std::fabs(
          sample.state.bodyFacing.GetAngle2D(sample.state.facing)));

      if (sample.command.useDesiredLookAt) {
        const Vector3 look_delta =
            (sample.command.desiredLookAt - sample.state.position).Get2D();
        if (look_delta.GetLength() > 0.0001f) {
          const Vector3 desired_look =
              look_delta.GetNormalized(sample.state.bodyFacing);
          bucket.absolute_desired_relative_angles.push_back(std::fabs(
              desired_look.GetAngle2D(sample.state.facing)));
          bucket.absolute_look_errors.push_back(std::fabs(
              sample.state.bodyFacing.GetAngle2D(desired_look)));
          ++bucket.look_samples;
        }
      }

      const PlayerKinematicState& actual = sample.player->GetKinematicState();
      const double turn = std::fabs(
          actual.bodyFacing.GetAngle2D(sample.state.bodyFacing));
      bucket.absolute_turns.push_back(turn);
      ++bucket.turn_samples;
      if (sample.player->IsEligibleForProceduralLocomotion() &&
          sample.player->GetCurrentAnim()->id == sample.anim_id) {
        bucket.absolute_continued_turns.push_back(turn);
        ++bucket.continued_turn_samples;
      }
    }
  }

  const auto mean = [](const std::vector<double>& values) {
    double sum = 0.0;
    for (double value : values) sum += value;
    return values.empty() ? 0.0 : sum / values.size();
  };
  const auto percentile = [](std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(
        std::floor(fraction * static_cast<double>(values.size() - 1)));
    return values[index];
  };

  int total_samples = 0;
  std::cout << "legacy body-facing by speed class (radians):\n";
  for (int index = 0; index < 4; ++index) {
    const Bucket& bucket = buckets[index];
    total_samples += bucket.samples;
    if (bucket.samples == 0) {
      std::cout << "  "
                << VelocityClassName(static_cast<e_Velocity>(index))
                << " samples=0\n";
      continue;
    }
    std::cout << "  " << VelocityClassName(static_cast<e_Velocity>(index))
              << " samples=" << bucket.samples
              << " mean_speed=" << (bucket.speed_sum / bucket.samples)
              << " abs_body_relative_mean="
              << mean(bucket.absolute_relative_angles)
              << " p90=" << percentile(bucket.absolute_relative_angles, 0.90)
              << " p99=" << percentile(bucket.absolute_relative_angles, 0.99)
              << " max=" << percentile(bucket.absolute_relative_angles, 1.0)
              << " look_samples=" << bucket.look_samples
              << " abs_desired_relative_mean="
              << mean(bucket.absolute_desired_relative_angles)
              << " desired_relative_p90="
              << percentile(bucket.absolute_desired_relative_angles, 0.90)
              << " desired_relative_p99="
              << percentile(bucket.absolute_desired_relative_angles, 0.99)
              << " desired_relative_max="
              << percentile(bucket.absolute_desired_relative_angles, 1.0)
              << " abs_body_to_look_mean="
              << mean(bucket.absolute_look_errors)
              << " body_to_look_p90="
              << percentile(bucket.absolute_look_errors, 0.90)
              << " body_to_look_p99="
              << percentile(bucket.absolute_look_errors, 0.99)
              << " body_to_look_max="
              << percentile(bucket.absolute_look_errors, 1.0)
              << " turn_samples=" << bucket.turn_samples
              << " turn_mean=" << mean(bucket.absolute_turns)
              << " turn_p90=" << percentile(bucket.absolute_turns, 0.90)
              << " turn_p99=" << percentile(bucket.absolute_turns, 0.99)
              << " turn_max=" << percentile(bucket.absolute_turns, 1.0)
              << " continued_turn_samples=" << bucket.continued_turn_samples
              << " continued_turn_p90="
              << percentile(bucket.absolute_continued_turns, 0.90)
              << " continued_turn_p99="
              << percentile(bucket.absolute_continued_turns, 0.99)
              << " continued_turn_max="
              << percentile(bucket.absolute_continued_turns, 1.0) << "\n";
  }
  Require(total_samples > 0,
          "body-facing measurement: no pure-locomotion samples were collected");
}

// H3e3b-prep2: evaluate explicit body-facing semantics in shadow. A forced
// reset is deliberately counted, never hidden: if locomotion facing moves the
// cone over a rate-limited body in one tick, finite turn rate and a strict cone
// bound cannot both hold. Such a grid point is not a valid authority candidate.
// H3e4e1: measure who currently owns the movement command lifetime. Root motion
// and body pose are gone, but an accepted Movement command still only arrives
// when an animation selection replaces the command in force. Measurement only;
// it never issues an extra controller query, because that is not proven pure.
void MeasureMovementCommandLifecycle(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "movement lifecycle: kickoff");
  // Scope the transient re-entry audit to this measurement corpus: telemetry is
  // not serialized, so earlier scenarios and state restore can pollute it.
  ResetLocomotionReentryAudits();
  ResetPlayerDecisionCadenceTelemetry();

  const int queries_before = HumanoidSchedulerQueries();
  const int material_before = HumanoidMaterialCommandCandidates();
  const int accepted_before = HumanoidMaterialCommandsAccepted();
  const int selections_before = HumanoidMovementSelections();
  const int switches_before = HumanoidMovementSwitches();
  const int requeues_before = HumanoidMovementRequeues();
  const int from_other_before = HumanoidMovementFromOtherAction();
  const size_t lifetimes_before = HumanoidMovementCommandLifetimes_ms().size();
  const size_t frames_before = HumanoidSelectedMovementFrames().size();
  const int pure_before = HumanoidProceduralMovementTicks();
  const int foot_selections_before = HumanoidFootCounterfactualSelections();
  const int foot_changed_before = HumanoidFootWinnerChanged();
  const int foot_frames_before = HumanoidFootFrameCountDiff();
  const int foot_quadrant_before = HumanoidFootQuadrantDiff();
  const int foot_velocity_before = HumanoidFootOutgoingVelocityDiff();
  const int foot_angle_bits_before = HumanoidFootOutgoingAngleBitsDiff();
  const int foot_angle_bucket_before = HumanoidFootOutgoingAngleBucketDiff();
  const int foot_special_before = HumanoidFootSpecialStateDiff();
  const int foot_lifecycle_before = HumanoidFootLifecycleChanged();
  const int direct_before = PlayerMovementCommandDirectAdoptions();
  const int legacy_only_opportunities_before = LegacyOnlyDecisionOpportunities();
  const int cadence_selection_suppressed_before =
      PlayerLocomotionCadenceSelectionSuppressed();
  const int legacy_only_queries_before = LegacyOnlyDecisionQueries();
  const int legacy_only_movement_queries_before = LegacyOnlyDecisionMovementQueries();
  const int legacy_only_publications_before = LegacyOnlyDecisionPublications();
  const int cadence_publications_before = DecisionPublicationCauseCount(0);
  const int continuity_repairs_before = ContinuityRepairAttempts();
  const int continuity_repair_publications_before =
      ContinuityRepairPublications();
  const int legacy_only_material_changes_before = LegacyOnlyDecisionMaterialChanges();
  const int legacy_only_movement_selections_before = LegacyOnlyDecisionMovementSelections();
  const int legacy_only_non_movement_selections_before =
      LegacyOnlyDecisionNonMovementSelections();
  const int legacy_only_no_selection_before = LegacyOnlyDecisionNoSelection();
  const int legacy_only_movement_selection_publications_before =
      LegacyOnlyDecisionMovementSelectionPublications();
  const int legacy_only_movement_selection_material_changes_before =
      LegacyOnlyDecisionMovementSelectionMaterialChanges();
  const int legacy_only_caused_queries_before = LegacyOnlyDecisionCausedQueries();
  const int simulation_cache_present_before = SimulationDecisionCachePresent();
  const int simulation_cache_missing_before = SimulationDecisionCacheMissing();
  const size_t simulation_cache_age_before = SimulationDecisionCacheAge_ms().size();
  const int simulation_live_has_movement_before = SimulationDecisionLiveHasMovement();
  const int simulation_cache_has_movement_before = SimulationDecisionCacheHasMovement();
  const int simulation_movement_equal_before = SimulationDecisionMovementEqual();
  const int simulation_movement_different_before = SimulationDecisionMovementDifferent();
  const int simulation_queue_identical_before = SimulationDecisionQueueIdentical();
  const size_t simulation_first_diff_before = SimulationDecisionFirstDiffIndex().size();
  const int simulation_proof_movement_proven_before = SimulationDecisionProofMovementProven();
  const int simulation_proof_movement_unproven_before = SimulationDecisionProofMovementUnproven();
  const int simulation_proof_action_proven_before = SimulationDecisionProofActionProven();
  const int simulation_proof_action_unproven_before = SimulationDecisionProofActionUnproven();
  const int simulation_proof_none_proven_before = SimulationDecisionProofNoneProven();
  const int simulation_proof_none_unproven_before = SimulationDecisionProofNoneUnproven();
  const int decision_clock_queries_before = PlayerDecisionClockQueries();
  const int decision_clock_periodic_before = PlayerDecisionClockPeriodicQueries();
  const int decision_clock_forced_before = PlayerDecisionClockForcedQueries();
  const int decision_queue_missing_before = PlayerDecisionClockQueueConsumersMissing();

  const int ticks = 400;
  for (int tick = 0; tick < ticks; ++tick) env.step();

  const int queries = HumanoidSchedulerQueries() - queries_before;
  const int material = HumanoidMaterialCommandCandidates() - material_before;
  const int material_accepted =
      HumanoidMaterialCommandsAccepted() - accepted_before;
  const int selections = HumanoidMovementSelections() - selections_before;
  const int switches = HumanoidMovementSwitches() - switches_before;
  const int requeues = HumanoidMovementRequeues() - requeues_before;
  const int from_other =
      HumanoidMovementFromOtherAction() - from_other_before;
  const int pure_ticks = HumanoidProceduralMovementTicks() - pure_before;
  const int continuity_repairs =
      ContinuityRepairAttempts() - continuity_repairs_before;
  const int continuity_repair_publications =
      ContinuityRepairPublications() - continuity_repair_publications_before;
  std::vector<int> lifetimes(
      HumanoidMovementCommandLifetimes_ms().begin() + lifetimes_before,
      HumanoidMovementCommandLifetimes_ms().end());
  std::vector<int> frames(HumanoidSelectedMovementFrames().begin() + frames_before,
                         HumanoidSelectedMovementFrames().end());

  Require(PlayerDecisionClockForcedQueries() - decision_clock_forced_before ==
              continuity_repairs,
          "a3b2: stale continuity repair did not force exactly one Player Decision query");
  Require(continuity_repair_publications == continuity_repairs,
          "a3b2: continuity repair did not publish exactly once per forced query");
  Require(pure_ticks > 0,
          "movement lifecycle: no pure locomotion tick was observed");
  Require(selections > 0,
          "movement lifecycle: no movement command was accepted");
  Require(switches + requeues + from_other == selections,
          "movement lifecycle: every acceptance must have exactly one reason");
  Require(queries >= selections,
          "movement lifecycle: an acceptance happened without a scheduler query");
  Require(static_cast<int>(lifetimes.size()) == switches + requeues,
          "movement lifecycle: lifetimes must match movement->movement");
  Require(static_cast<int>(frames.size()) == selections,
          "movement lifecycle: every acceptance must record one clip");

  const auto percentile = [](std::vector<int> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(std::floor(
        fraction * static_cast<double>(values.size() - 1)))];
  };
  const auto mean = [](const std::vector<int>& values) {
    double sum = 0.0;
    for (int value : values) sum += value;
    return values.empty() ? 0.0 : sum / values.size();
  };

  std::cout << "movement command lifecycle:\n";
  std::cout << "  pure_ticks=" << pure_ticks << " selections=" << selections
            << " switches=" << switches << " requeues=" << requeues
            << " from_other_action=" << from_other << "\n";
  std::cout << "  scheduler_queries=" << queries
            << " material_candidates=" << material
            << " material_accepted=" << material_accepted
            << " material_rejected=" << (material - material_accepted) << "\n";
  std::cout << "  selected_frame_count mean=" << mean(frames)
            << " p50=" << percentile(frames, 0.50)
            << " p90=" << percentile(frames, 0.90)
            << " p99=" << percentile(frames, 0.99)
            << " max=" << percentile(frames, 1.0) << "\n";
  std::cout << "  command_lifetime_ms mean=" << mean(lifetimes)
            << " p50=" << percentile(lifetimes, 0.50)
            << " p90=" << percentile(lifetimes, 0.90)
            << " p99=" << percentile(lifetimes, 0.99)
            << " max=" << percentile(lifetimes, 1.0) << "\n";

  // Pure-locomotion ticks driven by a non-Movement legacy command (BallControl or
  // Trap with useDesiredMovement). The oracle compares these too; the count is
  // reported because it is a real property of the legacy scheduler.
  std::cout << "  non_movement_locomotion_ticks="
            << PlayerMovementCommandNonMovementTicks() << "\n";
  std::cout << "  locomotion_gate both_true="
            << HumanoidLocomotionGateBothTrue()
            << " legacy_only=" << HumanoidLocomotionGateLegacyOnly()
            << " simulation_only=" << HumanoidLocomotionGateSimulationOnly()
            << " both_false=" << HumanoidLocomotionGateBothFalse() << "\n";
  const PlayerTickGateMismatchContext &simulation_only =
      PlayerTickGateMismatchFor(true);
  const PlayerTickGateMismatchContext &legacy_only =
      PlayerTickGateMismatchFor(false);
  const auto dump_gate_age = [](const PlayerTickGateMismatchContext &context,
                                  bool direct) {
    const int count = direct ? context.direct_age_count : context.reset_age_count;
    const long sum = direct ? context.direct_age_sum_ms : context.reset_age_sum_ms;
    const int maximum = direct ? context.direct_age_max_ms : context.reset_age_max_ms;
    std::cout << (count ? sum / count : -1) << "/" << maximum;
  };
  std::cout << "  player_tick_gate simulation_only=" << simulation_only.ticks
            << " source[action,direct,legacy,seed,fallback]="
            << simulation_only.simulation_source[0] << ","
            << simulation_only.simulation_source[1] << ","
            << simulation_only.simulation_source[2] << ","
            << simulation_only.simulation_source[3] << ","
            << simulation_only.simulation_source[4]
            << " direct_age_mean/max=";
  dump_gate_age(simulation_only, true);
  std::cout << " reset_age_mean/max=";
  dump_gate_age(simulation_only, false);
  std::cout << "\n";
  std::cout << "  player_tick_gate legacy_only=" << legacy_only.ticks
            << " shape[uninitialized,nonmovement,movement_no_desired,other]="
            << legacy_only.legacy_uninitialized << ","
            << legacy_only.legacy_non_movement << ","
            << legacy_only.legacy_movement_without_desired << ","
            << legacy_only.legacy_other
            << " direct_age_mean/max=";
  dump_gate_age(legacy_only, true);
  std::cout << " reset_age_mean/max=";
  dump_gate_age(legacy_only, false);
  std::cout << "\n";
  // Observation only: nonzero mismatches mean the execution-gate migration is
  // independently semantic and must be audited before it can be flipped.
  std::cout << "  command_source direct_movement_intent="
            << (PlayerMovementCommandDirectAdoptions() - direct_before) << "\n";
  std::cout << "  intent_cadence simulation_due="
            << PlayerLocomotionIntentDueTicks()
            << " legacy_opportunities="
            << PlayerLocomotionIntentLegacyOpportunityTicks()
            << " overlap=" << PlayerLocomotionIntentOverlapTicks()
            << " intent_consumed=" << PlayerLocomotionIntentConsumedTicks() << " due_ineligible="
            << PlayerLocomotionIntentDueIneligibleTicks()
            << " due_eligible="
            << (PlayerLocomotionIntentDueTicks() -
                PlayerLocomotionIntentDueIneligibleTicks())
            << "\n";
  std::cout << "  locomotion_cadence selection_suppressed="
            << (PlayerLocomotionCadenceSelectionSuppressed() -
                cadence_selection_suppressed_before) << "\n";
  // 4f-a1: animation-owned query pressure. legacy_only counts the requeue
  // opportunities where no simulation cadence was due; the goal is that the
  // clock becomes unreachable from that event, so these fall to zero.
  std::cout << "  decision_query_pressure legacy_only opportunities="
            << (LegacyOnlyDecisionOpportunities() - legacy_only_opportunities_before)
            << " queries=" << (LegacyOnlyDecisionQueries() - legacy_only_queries_before)
            << " movement_queries="
            << (LegacyOnlyDecisionMovementQueries() - legacy_only_movement_queries_before)
            << " publications="
            << (LegacyOnlyDecisionPublications() - legacy_only_publications_before)
            << " material_changes="
            << (LegacyOnlyDecisionMaterialChanges() - legacy_only_material_changes_before)
            << "\n";
  std::cout << "  decision_query_pressure selection movement="
            << (LegacyOnlyDecisionMovementSelections() - legacy_only_movement_selections_before)
            << " non_movement="
            << (LegacyOnlyDecisionNonMovementSelections() -
                legacy_only_non_movement_selections_before)
            << " none=" << (LegacyOnlyDecisionNoSelection() - legacy_only_no_selection_before)
            << "\n";
  std::cout << "  decision_query_pressure selected_movement publications="
            << (LegacyOnlyDecisionMovementSelectionPublications() -
                legacy_only_movement_selection_publications_before)
            << " material_changes="
            << (LegacyOnlyDecisionMovementSelectionMaterialChanges() -
                legacy_only_movement_selection_material_changes_before)
            << "\n";
  const int legacy_opportunity_delta =
      LegacyOnlyDecisionOpportunities() - legacy_only_opportunities_before;
  const int legacy_query_delta =
      LegacyOnlyDecisionQueries() - legacy_only_queries_before;
  const int legacy_publication_delta =
      LegacyOnlyDecisionPublications() - legacy_only_publications_before;
  Require(legacy_opportunity_delta > 0,
          "a3b3: animation opportunities must remain live");
  Require(legacy_query_delta == 0 &&
              LegacyOnlyDecisionMovementQueries() -
                      legacy_only_movement_queries_before == 0,
          "a3b3: legacy-only opportunities still called RequestCommand");
  Require(legacy_publication_delta == 0,
          "a3b3: animation-only opportunities still published Movement");
  Require(PlayerLocomotionCadenceSelectionSuppressed() -
              cadence_selection_suppressed_before > 0,
          "a3b3: locomotion cadence did not suppress a selection-only opportunity");
  Require(DecisionPublicationCauseCount(0) - cadence_publications_before > 0,
          "a3b3: locomotion cadence did not publish cached Movement");
  Require(PlayerDecisionClockQueries() - decision_clock_queries_before ==
              PlayerDecisionClockPeriodicQueries() - decision_clock_periodic_before +
                  PlayerDecisionClockForcedQueries() - decision_clock_forced_before,
          "a3b1: Player Decision queries do not equal periodic plus forced");
  Require(PlayerDecisionClockQueueConsumersMissing() -
                  decision_queue_missing_before == 0,
          "a3b1: Player Decision queue missing before a consumer");
  // 4f-a2: could the animation requeue consume the last simulation-owned decision
  // instead of querying? Only a matching prefix is a proof, so unproven is not a
  // claim that the selection would change.
  {
    std::vector<int> ages = SimulationDecisionCacheAge_ms();
    ages.erase(ages.begin(), ages.begin() + simulation_cache_age_before);
    std::vector<int> first_diffs = SimulationDecisionFirstDiffIndex();
    first_diffs.erase(first_diffs.begin(),
                      first_diffs.begin() + simulation_first_diff_before);
    std::cout << "  decision_queue_shadow caused_queries="
              << (LegacyOnlyDecisionCausedQueries() - legacy_only_caused_queries_before)
              << " cache_present="
              << (SimulationDecisionCachePresent() - simulation_cache_present_before)
              << " cache_missing="
              << (SimulationDecisionCacheMissing() - simulation_cache_missing_before)
              << " age_p50=" << percentile(ages, 0.50)
              << " p90=" << percentile(ages, 0.90)
              << " p99=" << percentile(ages, 0.99)
              << " max=" << percentile(ages, 1.0) << "\n";
    std::cout << "  decision_queue_shadow live_has_movement="
              << (SimulationDecisionLiveHasMovement() - simulation_live_has_movement_before)
              << " cached_has_movement="
              << (SimulationDecisionCacheHasMovement() - simulation_cache_has_movement_before)
              << " movement_equal="
              << (SimulationDecisionMovementEqual() - simulation_movement_equal_before)
              << " movement_different="
              << (SimulationDecisionMovementDifferent() - simulation_movement_different_before)
              << " identical="
              << (SimulationDecisionQueueIdentical() - simulation_queue_identical_before)
              << " first_diff_n=" << first_diffs.size() << "\n";
    std::cout << "  decision_queue_proof movement proven="
              << (SimulationDecisionProofMovementProven() - simulation_proof_movement_proven_before)
              << " unproven="
              << (SimulationDecisionProofMovementUnproven() - simulation_proof_movement_unproven_before)
              << " action proven="
              << (SimulationDecisionProofActionProven() - simulation_proof_action_proven_before)
              << " unproven="
              << (SimulationDecisionProofActionUnproven() - simulation_proof_action_unproven_before)
              << " none proven="
              << (SimulationDecisionProofNoneProven() - simulation_proof_none_proven_before)
              << " unproven="
              << (SimulationDecisionProofNoneUnproven() - simulation_proof_none_unproven_before)
              << "\n";
    std::cout << "  decision_queue_first_diff p50=" << percentile(first_diffs, 0.50)
              << " p90=" << percentile(first_diffs, 0.90)
              << " max=" << percentile(first_diffs, 1.0) << "\n";
  }
  // 4f-a3-prep: intervals come only from consecutive actual RequestCommand calls
  // for the same player. No extra controller query is issued by this measurement.
  DumpPlayerDecisionCadenceTelemetry();
  std::cout << "  player_decision_clock queries="
            << (PlayerDecisionClockQueries() - decision_clock_queries_before)
            << " periodic="
            << (PlayerDecisionClockPeriodicQueries() - decision_clock_periodic_before)
            << " forced="
            << (PlayerDecisionClockForcedQueries() - decision_clock_forced_before)
            << " queue_missing="
            << (PlayerDecisionClockQueueConsumersMissing() - decision_queue_missing_before)
            << "\n";
  std::cout << "  authority direct_vs_legacy_equal="
            << DirectVsLegacyCommandEqual()
            << " direct_vs_legacy_materially_different="
            << DirectVsLegacyCommandMateriallyDifferent() << "\n";
  const int refresh_commits = HumanoidBasePathRefreshCommits();
  const int successful_publications =
      HumanoidIntentRefreshes() + HumanoidEligibilityGainRefreshes();
  DumpQueryOpportunities();
  std::cout << "  held_due movement+retains " << HeldDueMovementRetainsCandidate() << "/"
            << HeldDueMovementRetainsTicks() << " ballcontrol "
            << HeldDueBallControlCandidate() << "/" << HeldDueBallControlTicks()
            << " trap " << HeldDueTrapCandidate() << "/" << HeldDueTrapTicks()
            << " other " << HeldDueOtherCandidate() << "/" << HeldDueOtherTicks()
            << "\n";
  std::cout << "  player_path queries=" << PlayerPathControllerQueries()
            << " with_movement=" << PlayerPathQueriesWithMovement()
            << " suppressed_by_repair="
            << PlayerPathQueriesWithMovementSuppressedByRepair()
            << " publications=" << PlayerPathDirectPublications()
            << " commits=" << PlayerPathRefreshCommits()
            << " candidates_missing=" << PlayerPathCandidatesMissing() << "\n";
  std::cout << "  player_path_noquery_trip attempts="
            << PlayerPathLocalTripAttempts()
            << " selected=" << PlayerPathLocalTripSelected()
            << " movement_fallback_selected="
            << PlayerPathLocalTripMovementFallbackSelected()
            << " movement_fallback_scheduler_commits="
            << PlayerPathLocalTripMovementFallbackRefreshCommits() << "\n";
  Require(PlayerPathControllerQueries() == 0 &&
              PlayerPathQueriesWithMovement() == 0,
          "a3b1: legacy/locomotion RequestCommand caller remains in Humanoid::Process");
  Require(PlayerPathDirectPublications() == PlayerPathRefreshCommits(),
          "player path: a publication did not commit exactly once");
  Require(PlayerPathLocalTripMovementFallbackRefreshCommits() == 0,
          "player path: a local Trip Movement fallback consumed the refresh scheduler");
  Require(ContinuityRepairAttempts() == ContinuityRepairPublications(),
          "a3b1: continuity repair did not publish exactly once per forced decision");
  Require(ContinuityRepairCandidatesMissing() == 0,
          "a3b1: continuity repair forced query did not contain Movement");
  std::cout << "  intent_refreshes=" << HumanoidIntentRefreshes()
            << " candidates_missing=" << HumanoidIntentCandidatesMissing()
            << " gain_refreshes=" << HumanoidEligibilityGainRefreshes()
            << " gain_missing=" << HumanoidEligibilityGainCandidatesMissing()
            << " commits=" << refresh_commits
            << " commits_minus_publications=" << (refresh_commits - successful_publications)
            << "\n";
  Require(refresh_commits == successful_publications,
          "locomotion intent: a scheduler refresh was committed without a publication");
  for (int context = 0; context < kResetSituationCallContextCount; ++context) {
    std::cout << "  reset_situation_context "
              << ResetSituationCallContextName(context)
              << " simulation_only_ticks="
              << SimulationOnlyGateMismatchForResetContext(context) << "\n";
  }
  std::cout << "  decision_intent present=" << DecisionLocomotionIntentPresentTicks()
            << " missing=" << DecisionLocomotionIntentMissingTicks()
            << " missing_by_source[action,direct,legacy,seed,fallback]="
            << DecisionLocomotionIntentMissingForSource(0) << ","
            << DecisionLocomotionIntentMissingForSource(1) << ","
            << DecisionLocomotionIntentMissingForSource(2) << ","
            << DecisionLocomotionIntentMissingForSource(3) << ","
            << DecisionLocomotionIntentMissingForSource(4)
            << " direct_age_mean/max=";
  { const int count = DecisionLocomotionIntentAgeCount();
    std::cout << (count ? DecisionLocomotionIntentAgeSum_ms() / count : -1)
              << "/" << DecisionLocomotionIntentAgeMax_ms(); }
  std::cout << " present_if_sim_gate_instead="
            << DecisionLocomotionIntentPresentTicksLegacyGateFalse()
            << " missing_if_sim_gate_instead="
            << DecisionLocomotionIntentMissingTicksLegacyGateFalse()
            << " missing_there_by_source[action,direct,legacy,seed,fallback]="
            << DecisionLocomotionIntentMissingForSourceLegacyGateFalse(0) << ","
            << DecisionLocomotionIntentMissingForSourceLegacyGateFalse(1) << ","
            << DecisionLocomotionIntentMissingForSourceLegacyGateFalse(2) << ","
            << DecisionLocomotionIntentMissingForSourceLegacyGateFalse(3) << ","
            << DecisionLocomotionIntentMissingForSourceLegacyGateFalse(4)
            << "\n";
  std::cout << "  reentry_action_exit count=" << LocomotionActionExitCount()
            << "\n";
  for (int category = 1; category < 4; ++category) {
    const LocomotionReentryAudit &audit = LocomotionReentryAuditFor(category);
    if (audit.ticks == 0) continue;
    std::cout << "  reentry " << LocomotionReentryCategoryName(category)
              << " ticks=" << audit.ticks
              << " gen_advanced=" << audit.generation_advanced
              << " gen_unchanged=" << audit.generation_unchanged
              << " scheduler_due=" << audit.scheduler_due
              << " scheduler_not_due=" << audit.scheduler_not_due
              << " would_require_fresh=" << audit.would_require_fresh
              << " stale_after_reset=" << audit.stale_after_reset
              << " stale_after_action_exit=" << audit.stale_after_action_exit
              << " stale_unexplained=" << audit.stale_not_explained_by_reset
              << " decision_age_mean/max=";
    std::cout << (audit.decision_age_count
                      ? audit.decision_age_sum_ms / audit.decision_age_count
                      : -1)
              << "/" << audit.decision_age_max_ms << "\n";
  }
  // A3b2 allows an action re-entry to observe a stale epoch at tick start; the
  // continuity-forced Player Decision query and publication repair it before
  // locomotion executes. Other unexplained stale epochs remain forbidden.
  for (int category = 1; category < 4; ++category) {
    Require(LocomotionReentryAuditFor(category).stale_not_explained_by_reset == 0,
            "continuity epoch: stale state was neither reset- nor action-exit-attributed");
  }
  Require(LocomotionReentryAuditFor(2).stale_after_action_exit <=
              continuity_repairs,
          "a3b2: stale action re-entry exceeded continuity repairs");
  Require(LocomotionReentryAuditFor(2).ticks <= LocomotionActionExitCount(),
          "reentry audit: action re-entries exceed observed locomotion exits");
  Require(LocomotionNegativeDecisionAgeSamples() == 0,
          "reentry audit: a decision age was negative inside a clean epoch");
  std::cout << "  locomotion_negative_decision_age_samples="
            << LocomotionNegativeDecisionAgeSamples() << "\n";
  std::cout << "  continuity_repair attempts=" << ContinuityRepairAttempts()
            << " publications=" << ContinuityRepairPublications()
            << " candidates_missing=" << ContinuityRepairCandidatesMissing()
            << " cause[cadence,legacy,repair]=" << DecisionPublicationCauseCount(0)
            << "," << DecisionPublicationCauseCount(1)
            << "," << DecisionPublicationCauseCount(2) << "\n";
  std::cout << "  decision_publication via_simulation_cadence="
            << DecisionPublicationViaSimulationCadence()
            << " via_legacy_opportunity_only="
            << DecisionPublicationViaLegacyOpportunityOnly()
            << " while_ineligible=" << DecisionPublicationWhileIneligible()
            << "\n";
  std::cout << "  reentry_fresh via_simulation_cadence="
            << ReentryFreshViaSimulationCadence()
            << " via_legacy_opportunity_only="
            << ReentryFreshViaLegacyOpportunityOnly()
            << " while_ineligible=" << ReentryFreshWhileIneligible() << "\n";
  std::cout << "  movement_oracle source[action,direct,legacy,seed,fallback]="
            << MovementOracleConsumesForSource(0) << ","
            << MovementOracleConsumesForSource(1) << ","
            << MovementOracleConsumesForSource(2) << ","
            << MovementOracleConsumesForSource(3) << ","
            << MovementOracleConsumesForSource(4)
            << " action_coupled_legacy_mismatch=" << ActionCoupledLegacyMismatch()
            << "\n";

  // H3e4e2: strict counterfactual for the foot tie-break. The clone is taken
  // before the foot stable_sort, so a changed winner is caused by foot alone.
  const int foot_selections =
      HumanoidFootCounterfactualSelections() - foot_selections_before;
  const int foot_changed = HumanoidFootWinnerChanged() - foot_changed_before;
  const int foot_frames = HumanoidFootFrameCountDiff() - foot_frames_before;
  const int foot_quadrant = HumanoidFootQuadrantDiff() - foot_quadrant_before;
  const int foot_velocity =
      HumanoidFootOutgoingVelocityDiff() - foot_velocity_before;
  const int foot_angle_bits =
      HumanoidFootOutgoingAngleBitsDiff() - foot_angle_bits_before;
  const int foot_angle_bucket =
      HumanoidFootOutgoingAngleBucketDiff() - foot_angle_bucket_before;
  const int foot_special = HumanoidFootSpecialStateDiff() - foot_special_before;
  const int foot_lifecycle =
      HumanoidFootLifecycleChanged() - foot_lifecycle_before;
  Require(foot_selections > 0,
          "foot counterfactual: no movement selection was compared");
  Require(foot_changed >= foot_lifecycle,
          "foot counterfactual: a lifecycle change must be a winner change");
  std::cout << "foot tie-break counterfactual:\n";
  std::cout << "  movement_selections=" << foot_selections
            << " winner_changed_without_foot=" << foot_changed
            << " winner_change_rate=";
  std::cout << (static_cast<double>(foot_changed) / foot_selections) << "\n";
  std::cout << "  of_changed frameCount=" << foot_frames
            << " quadrant=" << foot_quadrant
            << " outgoing_velocity=" << foot_velocity
            << " outgoing_angle_bits=" << foot_angle_bits
            << " outgoing_angle_bucket=" << foot_angle_bucket
            << " special_state=" << foot_special << "\n";
  std::cout << "  lifecycle_change_rate=";
  std::cout << (static_cast<double>(foot_lifecycle) / foot_selections) << "\n";
}


void MeasureBodyFacingShadowGrid(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "body-facing shadow grid: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  struct Pending { Player* player; PlayerKinematicState state; PlayerCommand command; };
  struct Shadow {
    Player* player; PlayerKinematicState state; Vector3 previousTarget;
    bool hasPreviousTarget = false; int alignmentMs = -1;
    bool outside = false; int outsideDurationMs = 0; int outsideClass = -1;
    double outsidePeakExcess = 0.0;
    int facingJumpRecoveryMs = -1; int facingJumpClass = -1;
  };
  struct Cell {
    int samples = 0, lookSamples = 0, relativeClamps = 0, turnClamps = 0;
    int outsideEvents = 0, rapidFacingChanges = 0;
    double relativeSum = 0.0, lookErrorSum = 0.0, turnSum = 0.0;
    double maxTurn = 0.0, maxRapidFacingTurn = 0.0;
    std::vector<double> turns, outsideDurations, outsidePeakExcesses;
    std::vector<double> alignMs, facingJumpRecoveryMs;
  };
  struct Grid { PlayerBodyFacingParameters parameters; std::vector<Shadow> shadows; Cell cells[4]; };
  const float rates[] = {3.0f, 6.0f, 9.0f, 12.0f};
  const float angles[] = {pi * 0.25f, pi * 0.5f, pi * 0.75f};
  Grid grids[12];
  for (int rate = 0; rate < 4; ++rate) for (int angle = 0; angle < 3; ++angle) {
    Grid& grid = grids[rate * 3 + angle];
    grid.parameters.maxTurnRate = rates[rate];
    grid.parameters.maxRelativeAngle = angles[angle];
  }

  const auto findShadow = [](Grid& grid, Player* player,
                             const PlayerKinematicState& initial) -> Shadow& {
    for (Shadow& shadow : grid.shadows) if (shadow.player == player) return shadow;
    Shadow shadow;
    shadow.player = player;
    shadow.state = initial;
    shadow.state.bodyFacing = initial.facing;  // valid model seed
    grid.shadows.push_back(shadow);
    return grid.shadows.back();
  };
  const int ticks = 400;
  for (int tick = 0; tick < ticks; ++tick) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(match->FirstTeam(), players);
    match->GetActiveTeamPlayers(match->SecondTeam(), players);
    for (Grid& grid : grids) {
      grid.shadows.erase(std::remove_if(grid.shadows.begin(), grid.shadows.end(),
          [](const Shadow& shadow) { return !shadow.player->IsEligibleForProceduralLocomotion(); }),
          grid.shadows.end());
    }

    std::vector<Pending> pending;
    for (Player* player : players) {
      if (!player->IsEligibleForProceduralLocomotion()) continue;
      const Anim* anim = player->GetCurrentAnim();
      const PlayerKinematicState state = player->GetKinematicState();
      pending.push_back(Pending{player, state, anim->originatingCommand});
      for (Grid& grid : grids) findShadow(grid, player, state);
    }
    env.step();

    for (const Pending& sample : pending) {
      const PlayerKinematicState& actual = sample.player->GetKinematicState();
      const int velocityClass = static_cast<int>(FloatToEnumVelocity(
          actual.velocity.GetLength()));
      for (Grid& grid : grids) {
        Shadow& shadow = findShadow(grid, sample.player, sample.state);
        shadow.state.position = actual.position;
        shadow.state.velocity = actual.velocity;
        shadow.state.facing = actual.facing;
        shadow.state.speed = actual.speed;
        Cell& cell = grid.cells[velocityClass];
        ++cell.samples;

        PlayerBodyFacingInput input;
        input.desiredFacing = actual.facing;
        bool hasLook = false;
        if (sample.command.useDesiredLookAt) {
          const Vector3 delta = (sample.command.desiredLookAt - actual.position).Get2D();
          if (delta.GetLength() > 0.0001f) {
            input.desiredFacing = delta.GetNormalized(actual.facing);
            hasLook = true;
          }
        }
        const Vector3 target = PlayerBodyFacing::AllowedTarget(
            shadow.state, input, grid.parameters);
        const float requestedRelative = std::fabs(
            input.desiredFacing.GetAngle2D(actual.facing));
        if (hasLook && requestedRelative >
            grid.parameters.maxRelativeAngle + kFloatTolerance) {
          ++cell.relativeClamps;
        }
        if (shadow.hasPreviousTarget && std::fabs(
                target.GetAngle2D(shadow.previousTarget)) > pi * 0.5f) {
          shadow.alignmentMs = 0;
        }
        shadow.previousTarget = target; shadow.hasPreviousTarget = true;

        // The cone constrains the target, not current state. If locomotion
        // moves underneath a finite-rate torso, record the transient overshoot
        // rather than hiding it with a discontinuous corrective snap.
        const Vector3 previous = shadow.state.bodyFacing;
        const float requestedTurn = std::fabs(target.GetAngle2D(previous));
        if (requestedTurn > grid.parameters.maxTurnRate * 0.01f + kFloatTolerance) {
          ++cell.turnClamps;
        }
        PlayerBodyFacing::Step(shadow.state, input, grid.parameters, 0.01f);
        const double turn = std::fabs(
            shadow.state.bodyFacing.GetAngle2D(previous));
        const double relative = std::fabs(
            shadow.state.bodyFacing.GetAngle2D(actual.facing));
        cell.turnSum += turn; cell.relativeSum += relative;
        cell.turns.push_back(turn);
        cell.maxTurn = std::max(cell.maxTurn, turn);
        const double overshoot = std::max(
            0.0, relative - grid.parameters.maxRelativeAngle);
        const bool outside = overshoot > kFloatTolerance;
        if (outside) {
          if (!shadow.outside) {
            shadow.outside = true; shadow.outsideDurationMs = 0;
            shadow.outsideClass = velocityClass; shadow.outsidePeakExcess = 0.0;
            ++cell.outsideEvents;
          }
          shadow.outsideDurationMs += 10;
          shadow.outsidePeakExcess = std::max(shadow.outsidePeakExcess, overshoot);
        } else if (shadow.outside) {
          Cell& eventCell = grid.cells[shadow.outsideClass];
          eventCell.outsideDurations.push_back(shadow.outsideDurationMs);
          eventCell.outsidePeakExcesses.push_back(shadow.outsidePeakExcess);
          shadow.outside = false; shadow.outsideClass = -1;
        }
        if (hasLook) {
          ++cell.lookSamples;
          cell.lookErrorSum += std::fabs(
              shadow.state.bodyFacing.GetAngle2D(input.desiredFacing));
        }
        const bool rapidFacingJump =
            std::fabs(actual.facing.GetAngle2D(sample.state.facing)) > pi * 0.5f;
        if (rapidFacingJump) {
          ++cell.rapidFacingChanges;
          cell.maxRapidFacingTurn = std::max(cell.maxRapidFacingTurn, turn);
          shadow.facingJumpRecoveryMs = 0;
          shadow.facingJumpClass = velocityClass;
        }
        if (shadow.facingJumpRecoveryMs >= 0) {
          if (outside) {
            shadow.facingJumpRecoveryMs += 10;
          } else if (shadow.facingJumpRecoveryMs > 0) {
            grid.cells[shadow.facingJumpClass].facingJumpRecoveryMs.push_back(
                shadow.facingJumpRecoveryMs);
            shadow.facingJumpRecoveryMs = -1; shadow.facingJumpClass = -1;
          } else {
            shadow.facingJumpRecoveryMs = -1; shadow.facingJumpClass = -1;
          }
        }
        if (shadow.alignmentMs >= 0) {
          if (std::fabs(shadow.state.bodyFacing.GetAngle2D(target)) <= 0.1f) {
            cell.alignMs.push_back(shadow.alignmentMs); shadow.alignmentMs = -1;
          } else { shadow.alignmentMs += 10; }
        }
      }
    }
  }

  const auto percentile = [](std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(std::floor(
        fraction * static_cast<double>(values.size() - 1)))];
  };
  int total = 0;
  std::cout << "body-facing shadow grid (radians; target cone, continuous body):\n";
  for (const Grid& grid : grids) {
    std::cout << "  rate=" << grid.parameters.maxTurnRate
              << " relative=" << grid.parameters.maxRelativeAngle << "\n";
    for (int index = 0; index < 4; ++index) {
      const Cell& cell = grid.cells[index]; total += cell.samples;
      if (cell.samples == 0) continue;
      std::cout << "    " << VelocityClassName(static_cast<e_Velocity>(index))
                << " n=" << cell.samples
                << " look_error=" << (cell.lookSamples ? cell.lookErrorSum / cell.lookSamples : 0.0)
                << " turn_p50=" << percentile(cell.turns, 0.50)
                << " turn_p90=" << percentile(cell.turns, 0.90)
                << " turn_max=" << cell.maxTurn
                << " target_clamp_rate="
                << (cell.lookSamples ?
                    static_cast<double>(cell.relativeClamps) / cell.lookSamples : 0.0)
                << " outside_events=" << cell.outsideEvents
                << " outside_rate=" << (static_cast<double>(cell.outsideEvents) / cell.samples)
                << " outside_duration_p50=" << percentile(cell.outsideDurations, 0.50)
                << " outside_duration_p90=" << percentile(cell.outsideDurations, 0.90)
                << " outside_duration_max=" << percentile(cell.outsideDurations, 1.0)
                << " outside_excess_p90=" << percentile(cell.outsidePeakExcesses, 0.90)
                << " outside_excess_max=" << percentile(cell.outsidePeakExcesses, 1.0)
                << " facing_jump_events=" << cell.rapidFacingChanges
                << " facing_jump_outside_events="
                << cell.facingJumpRecoveryMs.size()
                << " facing_jump_recovery_p50="
                << percentile(cell.facingJumpRecoveryMs, 0.50)
                << " facing_jump_recovery_p90="
                << percentile(cell.facingJumpRecoveryMs, 0.90)
                << " facing_jump_recovery_max="
                << percentile(cell.facingJumpRecoveryMs, 1.0)
                << " target_align_p50=" << percentile(cell.alignMs, 0.50)
                << " target_align_p90=" << percentile(cell.alignMs, 0.90)
                << " target_align_max=" << percentile(cell.alignMs, 1.0)
                << "\n";
    }
  }
  Require(total > 0, "body-facing shadow grid: no pure-locomotion samples");
}


// H3e1c-2: reachability must be derived from the same model as execution.
// Parallel measurement only: the AI still consumes its own heuristic and the
// simulation is untouched. For a fixed target it compares the legacy
// heuristic ETA, the procedural PlayerLocomotion ETA and the time the actor
// actually needed, to size the closed-loop error before either the planner or
// the executor is switched over.
void MeasureLocomotionPrediction(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "locomotion prediction: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  struct PendingPrediction {
    Player* player;
    Vector3 target;
    int start_step;
    int procedural_eta_ms;
    int legacy_eta_ms;
  };

  std::vector<PendingPrediction> pending;
  const float reach_radius = 0.9f;
  const int horizon_ms = 2000;
  // env.step() advances ten 10 ms physics ticks.
  const int step_ms = 100;

  int jobs = 0;
  int reached = 0;
  int unreached = 0;
  int comparable = 0;
  int procedural_reachable = 0;
  int legacy_reachable = 0;
  int procedural_closer = 0;
  int legacy_closer = 0;
  int reachability_disagreements = 0;
  int reachability_disagreements_commanded = 0;
  int procedural_reachable_commanded = 0;
  const int distance_bucket_count = 16;  // 2 m bins
  int distance_jobs[16] = {0};
  int distance_disagreements[16] = {0};
  double procedural_error_sum = 0.0;
  double legacy_error_sum = 0.0;

  const int steps = 400;
  for (int step = 0; step < steps; ++step) {
    if (step % 5 == 0 && pending.size() < 32) {
      std::vector<Player*> players;
      match->GetActiveTeamPlayers(match->FirstTeam(), players);
      match->GetActiveTeamPlayers(match->SecondTeam(), players);
      for (Player* player : players) {
        if (!player->IsEligibleForProceduralLocomotion()) continue;
        const PlayerCommand& command =
            player->GetCurrentAnim()->originatingCommand;
        if (!command.useDesiredMovement) continue;
        const float desired_speed = clamp(command.desiredVelocityFloat, 0.0f,
                                         player->GetMaxVelocity());
        if (desired_speed <= 0.0f) continue;

        const Vector3 target = match->GetBall()->Predict(300);
        const PlayerKinematicState start = player->GetKinematicState();
        PlayerLocomotionParameters parameters;
        parameters.maxSpeed = player->GetMaxVelocity();
        // The AI decides reachability assuming the actor commits to its maximum
        // speed, which is also what it passes to the legacy heuristic, so that
        // is the apples-to-apples comparison. The estimate at the currently
        // commanded speed is reported too, because that is what the actor is
        // actually doing.
        const int procedural_eta_sprint =
            PlayerLocomotion::EstimateArrival(
                start, target, parameters, player->GetMaxVelocity(),
                horizon_ms, reach_radius, reach_radius).usual_ms;
        const int procedural_eta_commanded =
            PlayerLocomotion::EstimateArrival(
                start, target, parameters, desired_speed, horizon_ms,
                reach_radius, reach_radius).usual_ms;
        const TimeNeeded legacy = AI_GetTimeNeededForDistance_ms(
            start.position, start.velocity, target,
            player->GetMaxVelocity(), true, horizon_ms);
        // Reachability is the question the closed loop actually asks, so count
        // disagreement over every sampled job instead of only over the few
        // that later happen to arrive.
        const bool legacy_reachable_now =
            legacy.usual_ms <= static_cast<unsigned int>(horizon_ms);
        const bool procedural_reachable_now = procedural_eta_sprint >= 0;
        if (procedural_reachable_now) ++procedural_reachable;
        if (procedural_eta_commanded >= 0) ++procedural_reachable_commanded;
        if (legacy_reachable_now) ++legacy_reachable;
        if (procedural_reachable_now != legacy_reachable_now) {
          ++reachability_disagreements;
        }
        if ((procedural_eta_commanded >= 0) != legacy_reachable_now) {
          ++reachability_disagreements_commanded;
        }
        // Bucket by initial distance to tell a thin boundary band apart from a
        // systematic mismatch.
        const int distance_bucket = clamp(
            static_cast<int>(start.position.GetDistance(target) / 2.0f), 0,
            distance_bucket_count - 1);
        ++distance_jobs[distance_bucket];
        if (procedural_reachable_now != legacy_reachable_now) {
          ++distance_disagreements[distance_bucket];
        }
        pending.push_back(PendingPrediction{player, target, step,
                                           procedural_eta_sprint,
                                           static_cast<int>(legacy.usual_ms)});
        ++jobs;
      }
    }

    env.step();

    for (size_t index = 0; index < pending.size();) {
      PendingPrediction& job = pending[index];
      if (!job.player->IsActive()) {
        pending.erase(pending.begin() + index);
        continue;
      }
      const int elapsed_ms = (step + 1 - job.start_step) * step_ms;
      const bool horizon_expired = elapsed_ms >= horizon_ms;
      const double distance =
          (job.target - job.player->GetPosition()).Get2D().GetLength();
      const bool arrived = distance <= reach_radius;

      if (arrived || horizon_expired) {
        if (arrived) {
          ++reached;
          const bool procedural_reached = job.procedural_eta_ms >= 0;
          const bool legacy_reached = job.legacy_eta_ms <= horizon_ms;
          if (procedural_reached && legacy_reached) {
            const double procedural_error =
                std::fabs(job.procedural_eta_ms - elapsed_ms);
            const double legacy_error =
                std::fabs(job.legacy_eta_ms - elapsed_ms);
            procedural_error_sum += procedural_error;
            legacy_error_sum += legacy_error;
            if (procedural_error < legacy_error) {
              ++procedural_closer;
            } else {
              ++legacy_closer;
            }
            ++comparable;
          }
        } else {
          ++unreached;
        }
        pending.erase(pending.begin() + index);
        continue;
      }
      ++index;
    }
  }

  Require(jobs > 0,
          "locomotion prediction: no reachability job was collected, the "
          "measurement would be vacuous");
  Require(comparable > 0,
          "locomotion prediction: no job had two comparable estimates");
  std::cout << "locomotion prediction: jobs=" << jobs << " reached="
            << reached << " unreached=" << unreached
            << " comparable=" << comparable
            << " mean_procedural_eta_error_ms="
            << (procedural_error_sum / comparable)
            << " mean_legacy_eta_error_ms=" << (legacy_error_sum / comparable)
            << " procedural_closer=" << procedural_closer
            << " legacy_closer=" << legacy_closer
            << " procedural_reachable_sprint=" << procedural_reachable
            << " procedural_reachable_commanded="
            << procedural_reachable_commanded
            << " legacy_reachable=" << legacy_reachable
            << " disagreements_sprint=" << reachability_disagreements
            << " (" << (100.0 * reachability_disagreements / jobs) << "%)"
            << " disagreements_commanded="
            << reachability_disagreements_commanded << " ("
            << (100.0 * reachability_disagreements_commanded / jobs) << "%)\n";
  for (int bucket = 0; bucket < distance_bucket_count; ++bucket) {
    if (distance_jobs[bucket] == 0) continue;
    std::cout << "  distance_bucket[" << (bucket * 2) << "m] jobs="
              << distance_jobs[bucket] << " disagreements="
              << distance_disagreements[bucket] << "\n";
  }
}

// H3e1c-3c: timeNeededToGetToBall_ms is now published by the procedural
// planner for pure locomotion, on the staggered 100 ms cadence. This compares
// that published value against a freshly solved intercept, so it quantifies
// planner STALENESS (the cost of the cache) rather than a
// legacy-versus-procedural gap. Measurement only.
void MeasureInterceptPrediction(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "intercept prediction: kickoff");
  Match* match = env.context->gameTask->GetMatch();
  Ball* ball = match->GetBall();

  // The legacy dual estimate uses a usual (touchable) and an optimistic radius,
  // so both are mirrored here instead of comparing one radius against two.
  const float usual_radius = 0.28f;
  const float optimistic_radius = 0.9f;
  const int horizon_ms = 2000;
  int samples = 0;
  int legacy_unreachable = 0;
  int procedural_usual_unreachable = 0;
  int procedural_optimistic_unreachable = 0;
  int reachability_disagreements = 0;
  int legacy_committed_unreachable = 0;
  int procedural_committed_unreachable = 0;
  int both_reachable = 0;
  double error_sum = 0.0;

  const int steps = 400;
  for (int step = 0; step < steps; ++step) {
    if (step % 5 == 0) {
      std::vector<Player*> players;
      match->GetActiveTeamPlayers(match->FirstTeam(), players);
      match->GetActiveTeamPlayers(match->SecondTeam(), players);
      for (Player* player : players) {
        if (!player->IsEligibleForProceduralLocomotion()) continue;
        // 4f-b2: sample by simulation eligibility, not by the animation's
        // originating Movement command. This deliberately expands the audit corpus.
        PlayerLocomotionParameters parameters;
        parameters.maxSpeed = player->GetMaxVelocity();
        const PlayerLocomotionReach procedural =
            PlayerLocomotion::EstimateEarliestInterceptExact(
                player->GetKinematicState(),
                [ball](int ms) { return ball->Predict(ms); }, parameters,
                player->GetMaxVelocity(), horizon_ms, usual_radius,
                optimistic_radius);
        // Compare like with like: timeNeededToGetToBall_ms is the legacy usual
        // estimate, so it is matched against the procedural usual estimate.
        const int legacy = player->GetTimeNeededToGetToBall_ms();
        const bool legacy_reachable = legacy < horizon_ms;
        const bool procedural_reachable = procedural.usual_ms >= 0;
        const bool procedural_optimistic_reachable =
            procedural.optimistic_ms >= 0;

        ++samples;
        if (!legacy_reachable) ++legacy_unreachable;
        if (!procedural_reachable) ++procedural_usual_unreachable;
        if (!procedural_optimistic_reachable) {
          ++procedural_optimistic_unreachable;
        }
        if (legacy_reachable != procedural_reachable) {
          ++reachability_disagreements;
        }
        // The dangerous class: the AI is confident it arrives quickly while the
        // model that will execute the movement says it cannot arrive at all.
        if (legacy < 1000 && !procedural_reachable) {
          ++legacy_committed_unreachable;
        }
        if (procedural_reachable && procedural.usual_ms < 1000 &&
            !legacy_reachable) {
          ++procedural_committed_unreachable;
        }
        if (legacy_reachable && procedural_reachable) {
          ++both_reachable;
          error_sum += std::fabs(legacy - procedural.usual_ms);
        }
      }
    }
    env.step();
  }

  Require(samples > 0,
          "intercept prediction: no sample was collected, the measurement "
          "would be vacuous");
  std::cout << "planner staleness: samples=" << samples
            << " procedural_reachable_usual="
            << (samples - procedural_usual_unreachable) << " ("
            << (100.0 * (samples - procedural_usual_unreachable) / samples)
            << "%)"
            << " procedural_reachable_optimistic="
            << (samples - procedural_optimistic_unreachable) << " ("
            << (100.0 * (samples - procedural_optimistic_unreachable) /
                samples)
            << "%)"
            << " legacy_reachable=" << (samples - legacy_unreachable) << " ("
            << (100.0 * (samples - legacy_unreachable) / samples) << "%)"
            << " reachability_disagreements=" << reachability_disagreements
            << " (" << (100.0 * reachability_disagreements / samples) << "%)"
            << " legacy_under_1000_but_unreachable="
            << legacy_committed_unreachable
            << " procedural_under_1000_but_legacy_unreachable="
            << procedural_committed_unreachable
            << " both_reachable=" << both_reachable
            << " mean_abs_eta_error_ms="
            << (both_reachable > 0 ? error_sum / both_reachable : 0.0)
            << "\n";
}

// H3e1c-3c: the planner samples the locomotion model on a staggered 100 ms
// cadence. This records the real cost and proves the schedule stays bounded, so
// a later change from 100 to 50 ms can be judged on numbers instead of feel.
void CheckReachabilityCadence(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "planner cadence: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  const int refresh_ticks = 10;
  int eligible_actor_ticks = 0;
  const int refreshes_before = PlayerReachabilityRefreshes();
  const int reuses_before = PlayerReachabilityReuses();
  const int eligible_ticks_before = PlayerReachabilityEligibleTicks();
  const int procedural_movement_before = HumanoidProceduralMovementTicks();
  const int legacy_body_pose_on_procedural_before =
      HumanoidLegacyBodyPoseSamplesOnProceduralMovement();
  const int legacy_body_pose_nonprocedural_before =
      HumanoidLegacyBodyPoseSamplesOnNonProceduralMovement();
  int max_cache_age_ms = 0;
  const int samples = 60;
  const int solver_calls_before = PlayerLocomotionInterceptSolverCalls();
  for (int sample = 0; sample < samples; ++sample) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(match->FirstTeam(), players);
    match->GetActiveTeamPlayers(match->SecondTeam(), players);
    // The planner runs on every 10 ms physics tick, so the window counts actor
    // ticks, not environment steps (one environment step is ten of them).
    const int first_tick = static_cast<int>(match->GetActualTime_ms() / 10);
    for (int offset = 0; offset < refresh_ticks; ++offset) {
      const int tick = first_tick + offset;
      for (Player* player : players) {
        if (!player->IsEligibleForProceduralLocomotion()) continue;
        ++eligible_actor_ticks;
        max_cache_age_ms = std::max(
            max_cache_age_ms,
            ((tick + player->GetStableID()) % refresh_ticks) * 10);
      }
    }
    env.step();
  }
  const int solver_calls =
      PlayerLocomotionInterceptSolverCalls() - solver_calls_before;
  const int refreshes = PlayerReachabilityRefreshes() - refreshes_before;
  const int reuses = PlayerReachabilityReuses() - reuses_before;
  const int procedural_movement_ticks =
      HumanoidProceduralMovementTicks() - procedural_movement_before;
  const int legacy_body_pose_on_procedural =
      HumanoidLegacyBodyPoseSamplesOnProceduralMovement() -
      legacy_body_pose_on_procedural_before;
  const int legacy_body_pose_nonprocedural =
      HumanoidLegacyBodyPoseSamplesOnNonProceduralMovement() -
      legacy_body_pose_nonprocedural_before;
  Require(procedural_movement_ticks > 0,
          "H3e4b: no procedural movement tick was observed");
  Require(legacy_body_pose_on_procedural == 0,
          "H3e4b: legacy body pose sampled during procedural movement");
  Require(legacy_body_pose_nonprocedural > 0,
          "H3e4b: non-procedural legacy body pose was not exercised");

  Require(eligible_actor_ticks > 0,
          "planner cadence: no eligible actor tick was collected, the "
          "measurement would be vacuous");
  Require(solver_calls > 0,
          "planner cadence: the procedural planner never ran, so pure "
          "locomotion planning is not actually in use");
  Require(max_cache_age_ms < refresh_ticks * 10,
          "planner cadence: the staggered cache age must stay under one "
          "refresh interval");
  // One solve per actor per refresh interval, so the expected ratio is 1/10.
  // This is the number that makes a later 100 -> 50 ms change judgeable.
  const double calls_per_actor_tick =
      static_cast<double>(solver_calls) / eligible_actor_ticks;
  Require(calls_per_actor_tick < 0.25,
          "planner cadence: the intercept solver is running more often than "
          "the staggered 100 ms schedule allows");
  // P1c: the real cache contract. Every eligible actor tick must either
  // refresh the capability estimate or reuse the previous one. Before this
  // check existed, the non-refresh ticks silently fell back to the default
  // heuristic while the cadence metrics still looked healthy.
  Require(refreshes > 0,
          "reachability cache: the solver never ran, so there is nothing to cache");
  Require(reuses > 0,
          "reachability cache: no estimate was ever reused, so the cache does nothing");
  const int engine_eligible_ticks =
      PlayerReachabilityEligibleTicks() - eligible_ticks_before;
  Require(refreshes + reuses == engine_eligible_ticks,
          "reachability cache: refreshes=" + std::to_string(refreshes) +
              " reuses=" + std::to_string(reuses) + " eligible=" +
              std::to_string(engine_eligible_ticks));
  std::cout << "reachability cache: engine_eligible_ticks="
            << engine_eligible_ticks << " scheduled_refreshes=" << refreshes
            << " actual_cache_reuses=" << reuses
            << " solver_calls=" << solver_calls
            << " max_value_age_ms=" << max_cache_age_ms
            << " calls_per_actor_tick="
            << (static_cast<double>(solver_calls) / engine_eligible_ticks)
            << "\n";
}
// P1a: measure the hybrid long-horizon planner approximation against the exact
// solver before anything consumes it. Nothing here is read by the simulation, so
// golden stays exact. The point is to choose the exact/analytic boundary from
// numbers: error percentiles, reachability-decision flips and cost for several
// candidate horizons, so the chosen one is a Pareto point rather than a guess.
void MeasureHybridInterceptApproximation(GameEnv& env,
                                         ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "hybrid intercept: kickoff");
  Match* match = env.context->gameTask->GetMatch();
  Ball* ball = match->GetBall();

  const int candidate_horizons[] = {0,    100,  200,  300,  400,  500, 600,
                                     700,  800,  900,  1000, 1200, 1500};
  const bool steady_state_model[] = {true, true, true, true, true, true, true,
                                     true, true, true, true, true, true};
  const int horizon_count = 13;
  struct HorizonStats {
    int samples = 0;
    int exact_reachable = 0;
    int hybrid_reachable = 0;
    int reachable_to_unreachable = 0;
    int unreachable_to_reachable = 0;
    int exact_short_hybrid_long = 0;
    int hybrid_short_exact_long = 0;
    double signed_sum = 0.0;
    double abs_sum = 0.0;
    double hybrid_seconds = 0.0;
    std::vector<double> abs_errors;
    int samples_by_heading[5] = {0, 0, 0, 0, 0};
    int unreachable_to_reachable_by_heading[5] = {0, 0, 0, 0, 0};
    int samples_by_speed_ratio[4] = {0, 0, 0, 0};
    int unreachable_to_reachable_by_speed_ratio[4] = {0, 0, 0, 0};
    int u2r_by_heading_and_speed[5][4] = {{0}};
  };
  HorizonStats stats[13];
  double exact_seconds = 0.0;
  int exact_calls = 0;

  const int steps = 200;
  for (int step = 0; step < steps; ++step) {
    if (step % 5 == 0) {
      std::vector<Player*> players;
      match->GetActiveTeamPlayers(match->FirstTeam(), players);
      match->GetActiveTeamPlayers(match->SecondTeam(), players);
      for (Player* player : players) {
        if (!player->IsEligibleForProceduralLocomotion()) continue;
        // 4f-b2: do not filter a simulation-owned intercept audit with an
        // animation-originating Movement flag.
        PlayerLocomotionParameters parameters;
        parameters.maxSpeed = player->GetMaxVelocity();
        const auto target_at = [ball](int ms) { return ball->Predict(ms); };
        const PlayerKinematicState start = player->GetKinematicState();
        const float desired_speed = player->GetMaxVelocity();
        const int full_horizon = static_cast<int>(ballPredictionSize_ms);

        const auto exact_begin = std::chrono::steady_clock::now();
        const PlayerLocomotionReach exact =
            PlayerLocomotion::EstimateEarliestInterceptExact(
                start, target_at, parameters, desired_speed, full_horizon,
                kLocomotionUsualReachRadius,
                kLocomotionOptimisticReachRadius);
        const auto exact_end = std::chrono::steady_clock::now();
        exact_seconds +=
            std::chrono::duration<double>(exact_end - exact_begin).count();
        ++exact_calls;

        for (int index = 0; index < horizon_count; ++index) {
          HorizonStats& entry = stats[index];
          const auto hybrid_begin = std::chrono::steady_clock::now();
          const PlayerLocomotionReach hybrid =
              PlayerLocomotion::EstimateEarliestInterceptHybrid(
                  start, target_at, parameters, desired_speed, full_horizon,
                  candidate_horizons[index], kLocomotionUsualReachRadius,
                  kLocomotionOptimisticReachRadius, steady_state_model[index]);
          const auto hybrid_end = std::chrono::steady_clock::now();
          entry.hybrid_seconds +=
              std::chrono::duration<double>(hybrid_end - hybrid_begin).count();

          ++entry.samples;
          const bool exact_reachable = exact.usual_ms >= 0;
          const bool hybrid_reachable = hybrid.usual_ms >= 0;
          if (exact_reachable) ++entry.exact_reachable;
          if (hybrid_reachable) ++entry.hybrid_reachable;
          // Bucket the sample by the start state, so a surviving reachability
          // flip can be attributed to the heading change or to the speed it
          // already carried rather than guessed at.
          const Vector3 heading_reference = start.velocity.Get2D().GetNormalized(
              start.facing.Get2D().GetNormalized(Vector3(0, -1, 0)));
          const float heading_degrees =
              std::fabs(heading_reference.GetAngle2D(target_at(0).Get2D())) *
              180.0f / pi;
          int heading_bucket = 4;
          if (heading_degrees < 15.0f) {
            heading_bucket = 0;
          } else if (heading_degrees < 45.0f) {
            heading_bucket = 1;
          } else if (heading_degrees < 90.0f) {
            heading_bucket = 2;
          } else if (heading_degrees < 135.0f) {
            heading_bucket = 3;
          }
          const float speed_ratio_now =
              parameters.maxSpeed > 0.0f
                  ? start.velocity.GetLength() / parameters.maxSpeed
                  : 0.0f;
          const int ratio_bucket =
              clamp(static_cast<int>(speed_ratio_now * 4.0f), 0, 3);
          ++entry.samples_by_heading[heading_bucket];
          ++entry.samples_by_speed_ratio[ratio_bucket];
          if (exact_reachable && !hybrid_reachable) {
            ++entry.reachable_to_unreachable;
          } else if (!exact_reachable && hybrid_reachable) {
            ++entry.unreachable_to_reachable;
            ++entry.unreachable_to_reachable_by_heading[heading_bucket];
            ++entry.unreachable_to_reachable_by_speed_ratio[ratio_bucket];
            ++entry.u2r_by_heading_and_speed[heading_bucket][ratio_bucket];
          }
          if (exact_reachable && hybrid_reachable) {
            const double difference =
                static_cast<double>(hybrid.usual_ms - exact.usual_ms);
            entry.signed_sum += difference;
            entry.abs_sum += std::fabs(difference);
            entry.abs_errors.push_back(std::fabs(difference));
          }
          // The flips that change a decision class: whether the answer is under
          // a second, which is what the AI is most sensitive to.
          const bool exact_short = exact_reachable && exact.usual_ms < 1000;
          const bool hybrid_short = hybrid_reachable && hybrid.usual_ms < 1000;
          if (exact_short && !hybrid_short) ++entry.exact_short_hybrid_long;
          if (hybrid_short && !exact_short) ++entry.hybrid_short_exact_long;
        }
      }
    }
    env.step();
  }

  Require(stats[0].samples > 0,
          "hybrid intercept: no sample was collected, the measurement would be "
          "vacuous");
  Require(exact_calls > 0, "hybrid intercept: the exact reference never ran");

  std::cout << "hybrid intercept vs exact: samples=" << stats[0].samples
            << " exact_ms_per_call="
            << (exact_calls > 0 ? 1000.0 * exact_seconds / exact_calls : 0.0)
            << "\n";
  for (int index = 0; index < horizon_count; ++index) {
    HorizonStats& entry = stats[index];
    std::sort(entry.abs_errors.begin(), entry.abs_errors.end());
    const auto percentile = [&entry](double fraction) {
      if (entry.abs_errors.empty()) return 0.0;
      const size_t position =
          static_cast<size_t>(fraction * (entry.abs_errors.size() - 1));
      return entry.abs_errors[position];
    };
    const int comparable = static_cast<int>(entry.abs_errors.size());
    std::cout << "  exact_horizon_ms=" << candidate_horizons[index]
              << " steady_state=" << (steady_state_model[index] ? 1 : 0)
              << " hybrid_ms_per_call="
              << (entry.samples > 0
                      ? 1000.0 * entry.hybrid_seconds / entry.samples
                      : 0.0)
              << " exact_reachable=" << entry.exact_reachable
              << " hybrid_reachable=" << entry.hybrid_reachable
              << " reachable_to_unreachable=" << entry.reachable_to_unreachable
              << " unreachable_to_reachable=" << entry.unreachable_to_reachable
              << " exact_short_hybrid_long=" << entry.exact_short_hybrid_long
              << " hybrid_short_exact_long=" << entry.hybrid_short_exact_long
              << " comparable=" << comparable
              << " mean_signed_error_ms="
              << (comparable > 0 ? entry.signed_sum / comparable : 0.0)
              << " mean_abs_error_ms="
              << (comparable > 0 ? entry.abs_sum / comparable : 0.0)
              << " p50=" << percentile(0.50) << " p90=" << percentile(0.90)
              << " p99=" << percentile(0.99)
              << " max=" << (entry.abs_errors.empty() ? 0.0
                                                      : entry.abs_errors.back())
              << "\n";
    if (candidate_horizons[index] == 0 || candidate_horizons[index] == 700) {
      static const char* kHeadingLabels[5] = {"0-15", "15-45", "45-90", "90-135", "135-180"};
      static const char* kRatioLabels[4] = {"0-.25", ".25-.5", ".5-.75", ".75-1"};
      std::cout << "    u2r_by_heading:";
      for (int slot = 0; slot < 5; ++slot) {
        std::cout << " " << kHeadingLabels[slot] << "deg=" << entry.unreachable_to_reachable_by_heading[slot] << "/" << entry.samples_by_heading[slot];
      }
      std::cout << "\n    u2r_by_speed_ratio:";
      for (int slot = 0; slot < 4; ++slot) {
        std::cout << " " << kRatioLabels[slot] << "=" << entry.unreachable_to_reachable_by_speed_ratio[slot] << "/" << entry.samples_by_speed_ratio[slot];
      }
      std::cout << "\n";
      std::cout << "    u2r heading x speed matrix (rows heading, cols speed):\n";
      for (int h = 0; h < 5; ++h) {
        std::cout << "     " << kHeadingLabels[h];
        for (int r = 0; r < 4; ++r) {
          std::cout << "  " << entry.u2r_by_heading_and_speed[h][r];
        }
        std::cout << "\n";
      }
      std::cout << "\n";
    }
  }
}

void CheckResetAndStateRoundTrip(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  CheckKinematicMirrorConsistency(env, "kinematic mirror after reset");
  Advance(env, 300);
  const SharedInfo first_reset = env.get_info();

  env.reset(config, false);
  Advance(env, 300);
  RequireInfoEqual(env.get_info(), first_reset, "repeat reset");
  CheckActionStateOracle(env.context->gameTask->GetMatch(),
                         "action state oracle after reset");
  CheckKinematicMirrorConsistency(env, "kinematic mirror after repeat reset");

  Advance(env, 75);
  const std::string serialized = env.get_state("");
  Advance(env, 125);
  const SharedInfo expected_after_restore = env.get_info();
  const std::string digest_after_restore = CaptureSimulationDigest(env);

  env.set_state(serialized);
  CheckActionStateOracle(env.context->gameTask->GetMatch(),
                         "action state oracle immediately after state restore");
  CheckKinematicMirrorConsistency(
      env, "kinematic mirror immediately after state restore");
  Advance(env, 125);
  RequireInfoEqual(env.get_info(), expected_after_restore, "state round-trip");
  Require(CaptureSimulationDigest(env) == digest_after_restore,
          "state round-trip: simulation digest differs (presentation RNG must "
          "not be part of simulation state)");
  CheckActionStateOracle(env.context->gameTask->GetMatch(),
                         "action state oracle after state restore");
  CheckKinematicMirrorConsistency(env, "kinematic mirror after state restore");
}

// H3e4f-g0b-continuity-restore: the continuity epoch decides whether a tick
// issues an extra controller query, so a restore must reproduce that decision.
// Both branches run the same reset from the same saved state and must agree on
// the epochs, the repair/query deltas and the resulting digest. Only serialized
// state is compared; the audit generation is deliberately not part of this.
void CheckDecisionContinuityRestoreDeterminism(GameEnv& env,
                                               ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "continuity restore: kickoff");
  Advance(env, 50);
  const std::string serialized = env.get_state("");
  Match *snapshot_match = env.context->gameTask->GetMatch();
  std::vector<Player *> snapshot_players;
  snapshot_match->GetTeam(0)->GetActivePlayers(snapshot_players);
  Require(!snapshot_players.empty(), "decision restore: no active players");
  Player *snapshot_player = snapshot_players[0];
  Require(snapshot_player->HasPlayerDecisionQueue(),
          "decision restore: queue not initialized at save");
  const int snapshot_id = snapshot_player->GetStableID();
  const PlayerCommandQueue snapshot_queue = snapshot_player->GetPlayerDecisionQueue();
  const unsigned long long snapshot_generation =
      snapshot_player->GetPlayerDecisionGeneration();
  const int snapshot_time_ms = static_cast<int>(snapshot_match->GetActualTime_ms());
  const bool snapshot_due_fast =
      snapshot_player->IsPlayerDecisionRefreshDue(snapshot_time_ms, 20);
  const bool snapshot_due_slow =
      snapshot_player->IsPlayerDecisionRefreshDue(snapshot_time_ms, 240);

  struct BranchResult {
    std::string digest;
    unsigned long long continuity_epoch = 0;
    unsigned long long published_epoch = 0;
    int query_delta = 0;
    int repair_attempts = 0;
    int repair_publications = 0;
    int repair_missing = 0;
  };
  const auto run_branch = [&]() {
    Match *match = env.context->gameTask->GetMatch();
    std::vector<Player *> players;
    match->GetTeam(0)->GetActivePlayers(players);
    Require(!players.empty(), "continuity restore: no active players");
    Player *reference = players[0];
    const int queries_before = PlayerDecisionClockQueries();
    const int attempts_before = ContinuityRepairAttempts();
    const int publications_before = ContinuityRepairPublications();
    const int missing_before = ContinuityRepairCandidatesMissing();
    match->ResetSituation(match->GetBall()->Predict(0));
    Advance(env, 60);
    BranchResult result;
    result.digest = CaptureSimulationDigest(env);
    result.continuity_epoch = reference->GetDecisionLocomotionContinuityEpoch();
    result.published_epoch = reference->GetDecisionLocomotionPublishedEpoch();
    result.query_delta = PlayerDecisionClockQueries() - queries_before;
    result.repair_attempts = ContinuityRepairAttempts() - attempts_before;
    result.repair_publications =
        ContinuityRepairPublications() - publications_before;
    result.repair_missing =
        ContinuityRepairCandidatesMissing() - missing_before;
    return result;
  };

  const BranchResult first = run_branch();
  // The branch must actually exercise the repair path, or this test would pass
  // vacuously by comparing two runs that never touched the epoch.
  Require(first.repair_attempts > 0,
          "continuity restore: the reset branch never triggered a repair");
  Require(first.repair_publications == first.repair_attempts,
          "continuity restore: a repair attempt did not publish");
  Require(first.repair_missing == 0,
          "continuity restore: a repair attempt had no candidate");

  env.set_state(serialized);
  std::vector<Player *> restored_players;
  env.context->gameTask->GetMatch()->GetTeam(0)->GetActivePlayers(restored_players);
  Player *restored_player = nullptr;
  for (Player *candidate : restored_players) {
    if (candidate->GetStableID() == snapshot_id) restored_player = candidate;
  }
  Require(restored_player && restored_player->HasPlayerDecisionQueue(),
          "decision restore: serialized queue not initialized after load");
  Require(restored_player->GetPlayerDecisionGeneration() == snapshot_generation,
          "decision restore: queue generation differs after load");
  const PlayerCommandQueue &restored_queue = restored_player->GetPlayerDecisionQueue();
  Require(restored_queue.size() == snapshot_queue.size(),
          "decision restore: queue length differs after load");
  for (size_t i = 0; i < snapshot_queue.size(); ++i) {
    Require(PlayerCommandsDecisionEqual(restored_queue[i], snapshot_queue[i]),
            "decision restore: serialized command differs after load");
  }
  Require(restored_player->IsPlayerDecisionRefreshDue(snapshot_time_ms, 20) ==
              snapshot_due_fast &&
          restored_player->IsPlayerDecisionRefreshDue(snapshot_time_ms, 240) ==
              snapshot_due_slow,
          "decision restore: scheduler cadence differs after load");
  const BranchResult second = run_branch();

  Require(second.digest == first.digest,
          "continuity restore: digest differs after restore");
  Require(second.continuity_epoch == first.continuity_epoch,
          "continuity restore: continuity epoch differs after restore");
  Require(second.published_epoch == first.published_epoch,
          "continuity restore: published epoch differs after restore");
  Require(second.query_delta == first.query_delta,
          "continuity restore: controller query count differs after restore");
  Require(second.repair_attempts == first.repair_attempts,
          "continuity restore: repair attempts differ after restore");
  Require(second.repair_publications == first.repair_publications,
          "continuity restore: repair publications differ after restore");
  Require(second.repair_missing == first.repair_missing,
          "continuity restore: repair misses differ after restore");
  std::cout << "  decision_continuity_restore repairs=" << first.repair_attempts
            << " queries=" << first.query_delta
            << " continuity_epoch=" << first.continuity_epoch
            << " published_epoch=" << first.published_epoch << "\n";
}

// 4f-c: two independent branches from one serialized state, not two mutating
// SelectAnim calls on one state. This is a bounded perturbation observation:
// only identical decision/action inputs can support a locomotion comparison.
void CheckMovementAnimationPerturbation(GameEnv& env, ScenarioConfig& config,
                                       bool require_frame_count_difference) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "animation A/B: kickoff");
  Advance(env, 5);
  const std::string checkpoint = env.get_state("");
  MovementAnimationPerturbation &hook = MovementAnimationPerturbationAudit();
  hook = MovementAnimationPerturbation();
  struct Actor {
    int id;
    int anim_id;
    PlayerKinematicState kinematics;
    e_FunctionType action_type;
    int action_frame;
    int action_frame_count;
    bool eligible;
    unsigned long long queue_generation;
    unsigned long long continuity_epoch;
    unsigned long long published_epoch;
    PlayerCommand intent;
  };
  struct Tick {
    std::string digest;
    int time_ms;
    int decision_queries;
    std::vector<Actor> actors;
  };
  const auto run_branch = [&](bool perturb) {
    env.set_state(checkpoint);
    hook = MovementAnimationPerturbation();
    hook.require_frame_count_difference = require_frame_count_difference;
    hook.enabled = false;
    std::vector<Tick> ticks;
    for (int t = 0; t < 400; ++t) {
      // Ten unperturbed ticks establish a non-vacuous restore/equality prefix.
      hook.enabled = perturb && t >= 10;
      const int queries_before = PlayerDecisionClockQueries();
      env.step();
      Tick tick;
      tick.digest = CaptureSimulationDigest(env);
      tick.decision_queries = PlayerDecisionClockQueries() - queries_before;
      Match *match = env.context->gameTask->GetMatch();
      tick.time_ms = static_cast<int>(match->GetActualTime_ms());
      std::vector<Player *> players;
      match->GetActiveTeamPlayers(match->FirstTeam(), players);
      match->GetActiveTeamPlayers(match->SecondTeam(), players);
      for (Player *player : players) {
        tick.actors.push_back(Actor{
            player->GetStableID(), player->GetCurrentAnim()->id,
            player->GetKinematicState(), player->GetSimulationActionState().type,
            player->GetSimulationActionState().frame,
            player->GetSimulationActionState().frameCount,
            player->IsEligibleForProceduralLocomotion(),
            player->GetPlayerDecisionGeneration(),
            player->GetDecisionLocomotionContinuityEpoch(),
            player->GetDecisionLocomotionPublishedEpoch(),
            player->GetDecisionLocomotionIntent()});
      }
      ticks.push_back(std::move(tick));
    }
    hook.enabled = false;
    return ticks;
  };
  const std::vector<Tick> baseline = run_branch(false);
  const std::vector<Tick> alternative = run_branch(true);
  const MovementAnimationPerturbation event = hook;
  Require(event.applied && event.original_anim_id != event.alternative_anim_id,
          "animation A/B: no Movement foot-order winner changed");
  const std::vector<Tick> replay = run_branch(false);
  Require(baseline.size() == alternative.size() && baseline.size() == replay.size(),
          "animation A/B: branch lengths differ");
  for (size_t t = 0; t < baseline.size(); ++t) {
    const Tick &a = baseline[t], &b = replay[t];
    Require(a.digest == b.digest && a.decision_queries == b.decision_queries &&
                a.time_ms == b.time_ms && a.actors.size() == b.actors.size(),
            "animation A/B: baseline not deterministic after restore");
    for (size_t i = 0; i < a.actors.size(); ++i) {
      Require(a.actors[i].id == b.actors[i].id &&
                  a.actors[i].anim_id == b.actors[i].anim_id &&
                  a.actors[i].action_type == b.actors[i].action_type &&
                  a.actors[i].action_frame == b.actors[i].action_frame &&
                  a.actors[i].action_frame_count == b.actors[i].action_frame_count &&
                  a.actors[i].queue_generation == b.actors[i].queue_generation &&
                  a.actors[i].published_epoch == b.actors[i].published_epoch,
              "animation A/B: presentation or decision clock differs on replay");
    }
  }
  const auto actor_by_id = [&](const Tick &tick) -> const Actor * {
    for (const Actor &actor : tick.actors)
      if (actor.id == event.player_id) return &actor;
    return nullptr;
  };
  const auto same_vector = [](const Vector3 &a, const Vector3 &b) {
    for (int axis = 0; axis < 3; ++axis)
      if (FloatBits(a.coords[axis]) != FloatBits(b.coords[axis])) return false;
    return true;
  };
  const auto same_kinematics = [&](const Actor &a, const Actor &b) {
    return same_vector(a.kinematics.position, b.kinematics.position) &&
           same_vector(a.kinematics.velocity, b.kinematics.velocity) &&
           same_vector(a.kinematics.facing, b.kinematics.facing) &&
           same_vector(a.kinematics.bodyFacing, b.kinematics.bodyFacing) &&
           FloatBits(a.kinematics.speed) == FloatBits(b.kinematics.speed);
  };
  int event_tick = -1, first_digest = -1, first_kinematics = -1;
  int first_any_kinematics = -1, first_any_action = -1, first_any_queue = -1;
  int first_action = -1, first_queue = -1, first_clock = -1;
  int first_roster_or_time = -1;
  int same_input_samples = 0, same_input_kinematic_diffs = 0;
  for (size_t t = 0; t < baseline.size(); ++t) {
    const Tick &a = baseline[t], &b = alternative[t];
    if (a.time_ms != b.time_ms || a.actors.size() != b.actors.size()) {
      first_roster_or_time = t;
      break;  // No longer a common per-player comparison domain.
    }
    for (size_t i = 0; i < a.actors.size(); ++i) {
      const Actor &left = a.actors[i], &right = b.actors[i];
      if (left.id != right.id) {
        first_roster_or_time = t;
        break;
      }
      if (first_any_kinematics < 0 && !same_kinematics(left, right))
        first_any_kinematics = t;
      if (first_any_action < 0 &&
          (left.action_type != right.action_type ||
           left.action_frame != right.action_frame ||
           left.action_frame_count != right.action_frame_count))
        first_any_action = t;
      if (first_any_queue < 0 &&
          (left.queue_generation != right.queue_generation ||
           left.continuity_epoch != right.continuity_epoch ||
           left.published_epoch != right.published_epoch ||
           !PlayerCommandsDecisionEqual(left.intent, right.intent)))
        first_any_queue = t;
    }
    if (first_roster_or_time >= 0) break;
    const Actor *original = actor_by_id(a), *changed = actor_by_id(b);
    Require(original && changed, "animation A/B: selected actor missing");
    if (original->anim_id == event.original_anim_id &&
        changed->anim_id == event.alternative_anim_id && event_tick < 0)
      event_tick = static_cast<int>(t);
    if (first_digest < 0 && a.digest != b.digest) first_digest = t;
    if (first_kinematics < 0 && !same_kinematics(*original, *changed))
      first_kinematics = t;
    if (first_action < 0 &&
        (original->action_type != changed->action_type ||
         original->action_frame != changed->action_frame ||
         original->action_frame_count != changed->action_frame_count))
      first_action = t;
    if (first_queue < 0 &&
        (original->queue_generation != changed->queue_generation ||
         original->continuity_epoch != changed->continuity_epoch ||
         original->published_epoch != changed->published_epoch ||
         !PlayerCommandsDecisionEqual(original->intent, changed->intent)))
      first_queue = t;
    if (first_clock < 0 && a.decision_queries != b.decision_queries)
      first_clock = t;
    if (event_tick >= 0 && original->eligible && changed->eligible &&
        original->action_type == e_FunctionType_Movement &&
        changed->action_type == e_FunctionType_Movement &&
        original->action_frame == changed->action_frame &&
        original->action_frame_count == changed->action_frame_count &&
        original->queue_generation == changed->queue_generation &&
        original->continuity_epoch == changed->continuity_epoch &&
        original->published_epoch == changed->published_epoch &&
        PlayerCommandsDecisionEqual(original->intent, changed->intent)) {
      ++same_input_samples;
      if (!same_kinematics(*original, *changed))
        ++same_input_kinematic_diffs;
    }
  }
  // env.step() advances up to 100 ms of 10 ms engine ticks: the injection
  // timestamp is inside the first differing observation window.
  Require(event_tick >= 10 && alternative[event_tick].time_ms >= event.time_ms &&
              alternative[event_tick].time_ms - event.time_ms <= 100,
          "animation A/B: changed candidate missing: tick=" +
              std::to_string(event_tick) + " event_ms=" +
              std::to_string(event.time_ms) + " tick_ms=" +
              (event_tick < 0 ? "none" :
               std::to_string(alternative[event_tick].time_ms)));
  const auto not_before_event = [&](int tick) {
    return tick < 0 || tick >= event_tick;
  };
  Require(not_before_event(first_digest) && not_before_event(first_kinematics) &&
              not_before_event(first_any_kinematics) &&
              not_before_event(first_any_action) && not_before_event(first_any_queue) &&
              not_before_event(first_action) && not_before_event(first_queue) &&
              not_before_event(first_clock),
          "animation A/B: branches diverged before the perturbation event");
  Require(not_before_event(first_roster_or_time),
          "animation A/B: roster or clock diverged before perturbation");
  std::cout << "  animation_ab mode="
            << (require_frame_count_difference ? "frame_count" : "foot_order")
            << " event_tick=" << event_tick
            << " time_ms=" << event.time_ms << " player=" << event.player_id
            << " anim=" << event.original_anim_id << "->"
            << event.alternative_anim_id << " first_digest=" << first_digest
            << " first_kinematics=" << first_kinematics
            << " first_any_kinematics=" << first_any_kinematics
            << " first_any_action=" << first_any_action
            << " first_any_queue=" << first_any_queue
            << " first_action=" << first_action
            << " first_queue=" << first_queue
            << " first_clock=" << first_clock
            << " first_roster_or_time=" << first_roster_or_time
            << " same_input_samples=" << same_input_samples
            << " same_input_kinematic_diffs=" << same_input_kinematic_diffs
            << " status="
            << (first_digest >= 0 || first_any_kinematics >= 0 ||
                first_any_action >= 0 || first_any_queue >= 0 ||
                first_clock >= 0 || first_roster_or_time >= 0
                    ? "gameplay_diverged" : "equivalence_unproven")
            << "\n";
  if (require_frame_count_difference) {
    Require(first_any_action == event_tick && first_action == event_tick,
            "animation A/B: frame-count perturbation lost its lifecycle first cause");
    Require(first_digest >= event_tick &&
                first_any_kinematics > first_any_action,
            "animation A/B: frame-count divergence attribution changed");
  } else {
    Require(first_digest < 0 && first_any_kinematics < 0 &&
                first_any_action < 0 && first_any_queue < 0 &&
                first_clock < 0 && first_roster_or_time < 0 &&
                same_input_samples > 0 && same_input_kinematic_diffs == 0,
            "animation A/B: foot-order bounded equality corpus changed");
  }
  // This run can demonstrate a difference, but a finite matching prefix cannot
  // prove independence for all possible animation/action lifecycles.
}

// 5a1: observation only. Each delta is measured across one fresh match corpus,
// not across telemetry from earlier scenarios or restored branches.
void MeasureContactAuthority(GameEnv& env, ScenarioConfig& config) {
  const e_FunctionType types[] = {
      e_FunctionType_Shot, e_FunctionType_ShortPass,
      e_FunctionType_LongPass, e_FunctionType_HighPass,
      e_FunctionType_Trap, e_FunctionType_BallControl};
  const char *names[] = {
      "Shot", "ShortPass", "LongPass", "HighPass", "Trap", "BallControl"};
  std::vector<ContactAuthorityAudit> before;
  for (e_FunctionType type : types) before.push_back(ContactAuthorityFor(type));
  ContactAuthorityAuditEnabled() = true;
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "contact authority: kickoff");
  Advance(env, 1000);
  ContactAuthorityAuditEnabled() = false;
  const auto percentile = [](auto samples, double fraction) -> double {
    if (samples.empty()) return -1.0;
    std::sort(samples.begin(), samples.end());
    const size_t index = static_cast<size_t>((samples.size() - 1) * fraction);
    return samples[index];
  };
  int scheduled_total = 0, impulse_total = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    const ContactAuthorityAudit &old = before[i];
    const ContactAuthorityAudit &now = ContactAuthorityFor(types[i]);
    const int scheduled = now.scheduled - old.scheduled;
    const int reached = now.at_contact_frame - old.at_contact_frame;
    const int distance = now.distance_rejected - old.distance_rejected;
    const int height = now.height_rejected - old.height_rejected;
    const int reachable = now.physically_reachable - old.physically_reachable;
    const int impulse = now.impulse_calls - old.impulse_calls;
    const int suppressed = now.suppressed - old.suppressed;
    scheduled_total += scheduled;
    impulse_total += impulse;
    Require(reached == distance + height + reachable &&
                reached == impulse + suppressed && impulse <= reachable &&
                now.nonzero_impulse_requests - old.nonzero_impulse_requests <= impulse,
            std::string("contact authority: unbalanced contact stages for ") + names[i]);
    const auto frames = std::vector<int>(
        now.contact_frames.begin() + old.contact_frames.size(),
        now.contact_frames.end());
    const auto delays = std::vector<int>(
        now.delays_ms.begin() + old.delays_ms.size(), now.delays_ms.end());
    const auto elapsed = std::vector<int>(
        now.observed_elapsed_ms.begin() + old.observed_elapsed_ms.size(),
        now.observed_elapsed_ms.end());
    const auto heights = std::vector<float>(
        now.desired_ball_heights.begin() + old.desired_ball_heights.size(),
        now.desired_ball_heights.end());
    const auto distances = std::vector<float>(
        now.full_ball_distances.begin() + old.full_ball_distances.size(),
        now.full_ball_distances.end());
    const auto biases = std::vector<float>(
        now.bumpy_ride_biases.begin() + old.bumpy_ride_biases.size(),
        now.bumpy_ride_biases.end());
    const auto impulse_speeds = std::vector<float>(
        now.impulse_request_speeds.begin() + old.impulse_request_speeds.size(),
        now.impulse_request_speeds.end());
    Require(static_cast<int>(frames.size()) == scheduled &&
                static_cast<int>(delays.size()) == scheduled &&
                static_cast<int>(elapsed.size()) == reached &&
                static_cast<int>(heights.size()) == reached &&
                static_cast<int>(distances.size()) == reached &&
                static_cast<int>(biases.size()) == reached &&
                static_cast<int>(impulse_speeds.size()) == impulse,
            std::string("contact authority: sample count mismatch for ") + names[i]);
    std::cout << "  contact_dependency " << names[i]
              << " scheduled=" << scheduled << " at_frame=" << reached
              << " reachable=" << reachable << " rejected_distance=" << distance
              << " rejected_height=" << height << " impulse_calls=" << impulse
              << " nonzero_requests="
              << now.nonzero_impulse_requests - old.nonzero_impulse_requests
              << " suppressed=" << suppressed
              << " reachable_without_impulse="
              << now.reachable_without_impulse - old.reachable_without_impulse
              << " pass_fiddling=" << now.pass_fiddling - old.pass_fiddling
              << " knock_on=" << now.knock_on - old.knock_on
              << " command_target=" << now.command_target - old.command_target
              << " forced_target=" << now.forced_target - old.forced_target
              << " retain_override="
              << now.incoming_retain_override - old.incoming_retain_override
              << " geometry[offset,anim_positions]="
              << now.contact_position_offset_nonzero -
                     old.contact_position_offset_nonzero << ","
              << now.animation_positions_present - old.animation_positions_present
              << " profile[maxpower,difficulty,native_dir]="
              << now.max_power_profile_present - old.max_power_profile_present << ","
              << now.difficulty_profile_present - old.difficulty_profile_present << ","
              << now.native_ball_direction_present - old.native_ball_direction_present
              << " profile[bodypart,in_retain,out_retain]="
              << now.contact_bodypart_present - old.contact_bodypart_present << ","
              << now.incoming_retain_present - old.incoming_retain_present << ","
              << now.outgoing_retain_present - old.outgoing_retain_present
              << " frame_p50/p90=" << percentile(frames, 0.5) << "/"
              << percentile(frames, 0.9)
              << " delay_ms_p50/p90=" << percentile(delays, 0.5) << "/"
              << percentile(delays, 0.9)
              << " elapsed_ms_p50/p90=" << percentile(elapsed, 0.5) << "/"
              << percentile(elapsed, 0.9)
              << " desired_height_p50/p90=" << percentile(heights, 0.5) << "/"
              << percentile(heights, 0.9)
              << " distance_p50/p90=" << percentile(distances, 0.5) << "/"
              << percentile(distances, 0.9)
              << " bumpy_bias_p50/p90=" << percentile(biases, 0.5) << "/"
              << percentile(biases, 0.9)
              << " impulse_speed_p50/p90=" << percentile(impulse_speeds, 0.5) << "/"
              << percentile(impulse_speeds, 0.9) << "\n";
  }
  Require(scheduled_total > 0 && impulse_total > 0,
          "contact authority: corpus never scheduled and delivered a contact");
}

void CheckMatchTransitions(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  Require(!env.get_info().is_in_play, "kickoff should begin paused");
  WaitUntilInPlay(env, 30, "initial kickoff");
  Advance(env, 5);  // Let kickoff's set-piece relaxation period elapse.

  Match* match = env.context->gameTask->GetMatch();
  match->GetBall()->SetPosition(
      Vector3(0.0f, pitchHalfH + lineHalfW + 0.5f, 0.5f));
  match->GetBall()->SetMomentum(Vector3(0));
  env.step();
  Require(!match->IsInPlay(), "throw-in should pause play");
  if (match->GetReferee()->GetBuffer().desiredSetPiece != e_GameMode_ThrowIn) {
    std::ostringstream message;
    message << "sideline exit scheduled set piece "
            << match->GetReferee()->GetBuffer().desiredSetPiece;
    throw RegressionFailure(message.str());
  }
  WaitUntilInPlay(env, 60, "throw-in restart");
  Advance(env, 5);  // Let throw-in's set-piece relaxation period elapse.

  match = env.context->gameTask->GetMatch();
  const int score_before = match->GetScore(0) + match->GetScore(1);
  match->GetBall()->SetPosition(
      Vector3(pitchHalfW + lineHalfW - 0.3f, 0.0f, 0.5f));
  match->GetBall()->SetMomentum(Vector3(50.0f, 0.0f, 0.0f));
  env.step();
  Require(match->GetScore(0) + match->GetScore(1) == score_before + 1,
          "crossing the goal line should score");
  Require(!match->IsInPlay(), "goal should pause play for kickoff");
  WaitUntilInPlay(env, 40, "goal kickoff restart");
}

}  // namespace

// The animation import used to be described by the scene graph. D4c2 replaced
// that with ImportNode/ImportHierarchy. The golden snapshots pin the
// end-to-end result of the import, but they do not pin the shape of the
// imported hierarchy or the behaviour of its lazy derived transforms, so
// those are checked here directly.
//
// The three properties below are the ones the simulation actually depends on:
//
//   1. the body-part anchors, and the order they are visited in. The touch
//      computation walks them and keeps the first one that is closest to the
//      ball, so this order decides ties.
//   2. the anchor names are disjoint from the body-part joint names: only
//      <node> entries become joints, only <geometry> entries become anchors.
//      The touch type of a ball touch is derived from the anchor name.
//   3. the derived-transform cache is a pure cache: recomputing it after
//      invalidation reproduces the same bits. If invalidation were missed the
//      cache would go stale and the import would silently change.
void CheckImportHierarchy() {
  ImportLoader loader;
  ImportHierarchy hierarchy =
      loader.LoadObject("media/objects/players/player.object");
  ImportNode* root = hierarchy.root.get();
  Require(root != nullptr, "import: the loader returned no root node");

  // Anchor order as written in the asset, which is the order the old
  // Node::GetObjects() produced too: anchors of a node first, then the child
  // nodes depth-first.
  const std::vector<std::string> expected_anchors = {
      "pelvis",        "trunk",          "head",           "left_upperarm",
      "left_lowerarm",  "right_upperarm", "right_lowerarm",  "left_upperleg",
      "left_lowerleg",  "left_foot",      "right_upperleg",  "right_lowerleg",
      "right_foot"};
  Require(hierarchy.anchors.size() == expected_anchors.size(),
          "import: unexpected number of body-part anchors");
  for (size_t i = 0; i < expected_anchors.size(); ++i) {
    Require(hierarchy.anchors[i]->kind == ImportNodeKind::Anchor,
            "import: a body-part anchor is not marked as one");
    if (hierarchy.anchors[i]->GetName() != expected_anchors[i]) {
      std::ostringstream message;
      message << "import: anchor " << i << " is '"
              << hierarchy.anchors[i]->GetName() << "', expected '"
              << expected_anchors[i] << "'";
      throw RegressionFailure(message.str());
    }
  }

  // The joints the animation poses. AnimCollection renames the root to
  // "player" before building this map, so do the same here.
  root->SetName("player");
  ImportNodeMap node_map;
  BuildImportNodeMap(root, node_map);
  const char* expected_joints[] = {
      "middle",       "neck",           "left_thigh",   "right_thigh",
      "left_knee",    "right_knee",     "left_ankle",   "right_ankle",
      "left_shoulder", "right_shoulder", "left_elbow",   "right_elbow",
      "body",         "player"};
  for (const char* joint : expected_joints) {
    Require(LookupImportNode(node_map, BodyPartFromString(joint)) != nullptr,
            std::string("import: joint not in the node map: ") + joint);
  }
  // An anchor must never also be a joint: the maps are built from disjoint
  // XML elements, and BodyPartFromString would be fatal on an anchor name.
  for (ImportNode* anchor : hierarchy.anchors) {
    for (const char* joint : expected_joints) {
      Require(anchor->GetName() != joint,
              std::string("import: anchor name collides with a joint: ") +
                  joint);
    }
  }

  // Derived transforms: a recomputation must reproduce the cached value
  // exactly, for every anchor. This is the property that makes the lazy cache
  // safe to use from the animation code.
  std::vector<Vector3> cached;
  for (const ImportNode* anchor : hierarchy.anchors) {
    cached.push_back(anchor->GetDerivedPosition());
  }
  root->UpdateDerivedTransforms();
  for (size_t i = 0; i < hierarchy.anchors.size(); ++i) {
    const Vector3 recomputed = hierarchy.anchors[i]->GetDerivedPosition();
    for (int axis = 0; axis < 3; ++axis) {
      Require(recomputed.coords[axis] == cached[i].coords[axis],
              "import: the derived transform cache is not a pure cache");
    }
  }

  // The hierarchy is really applied: moving the absolute root translates every
  // anchor by exactly that offset, and rotating a joint moves the anchors
  // below it.
  const Vector3 offset(3.0f, -2.0f, 0.5f);
  const Vector3 before = hierarchy.anchors.front()->GetDerivedPosition();
  root->SetPosition(offset);
  const Vector3 after = hierarchy.anchors.front()->GetDerivedPosition();
  for (int axis = 0; axis < 3; ++axis) {
    RequireNear(after.coords[axis], before.coords[axis] + offset.coords[axis],
                "import: translating the root did not translate an anchor");
  }

  //
  // Rotating a joint must move the anchors below it. Note that an anchor with a
  // zero local offset (which is how the asset writes most of them: a named
  // marker at the joint origin) sits exactly on its parent, so the rotation has
  // to come from further up the chain -- the head anchor moves with the middle
  // joint because the neck in between has a non-zero offset.
  ImportNode* middle_joint =
      LookupImportNode(node_map, BodyPartFromString("middle"));
  Require(middle_joint != nullptr, "import: no middle joint to rotate");
  const Vector3 head_before = hierarchy.anchors[2]->GetDerivedPosition();
  Quaternion rotated;
  rotated.SetAngleAxis(1.0f, Vector3(0, 0, 1));
  middle_joint->SetRotation(rotated);
  const Vector3 head_after = hierarchy.anchors[2]->GetDerivedPosition();
  Require(head_before != head_after,
          "import: rotating a joint did not move the anchors below it");
}


// reverse_team_processing swaps which team Match::Process() handles first
// (first_team = 1, second_team = 0) and shifts every frame toggle with it.
// Nothing else ran that configuration: every other check in this file uses the
// default order, so both the swapped processing order and its frame
// bookkeeping were entirely unexercised.
//
// This locks two properties, both observable inside a single run:
//
//   1. the canonical world frame is restored by every tick. Whichever team
//      was processed first, Match::Process() must return with the ball and
//      both teams unmirrored. The mirrored frame is a private mid-tick device
//      for the AI and collision code; it must never leak into state that a
//      caller can observe.
//   2. the configuration is self-reproducible: same seed, same scenario, same
//      tick count -> same observable state and the same simulation digest.
//
// It deliberately does NOT require reverse=true and reverse=false to follow
// the same trajectory. The processing order legitimately affects the legacy
// simulation, so that equality is not a real invariant and asserting it would
// encode an accident as a contract.
void CheckReverseTeamProcessing(GameEnv& env, ScenarioConfig& config) {
  const bool saved_reverse = config.reverse_team_processing;
  config.reverse_team_processing = true;

  // Phase 1: per-tick frame invariant.
  env.reset(config, false);
  Match* match = env.context->gameTask->GetMatch();
  Require(match->FirstTeam() == 1 && match->SecondTeam() == 0,
          "reverse_team_processing must process team 1 first");

  // Match::Process() is the simulation tick, and one env.step() is
  // physics_steps_per_frame of them. The per-tick invariant is therefore
  // checked by driving Process() directly: a leak that cancels inside a
  // single env.step() would be invisible at the step boundary.
  for (int i = 0; i < 600; ++i) {
    match->Process();
    const bool canonical = !match->isBallMirrored() &&
                           !match->GetTeam(0)->isMirrored() &&
                           !match->GetTeam(1)->isMirrored();
    if (!canonical) {
      std::ostringstream message;
      message << "reverse_team_processing: tick " << i
              << " did not restore the canonical frame (ball "
              << match->isBallMirrored() << ", team0 "
              << match->GetTeam(0)->isMirrored() << ", team1 "
              << match->GetTeam(1)->isMirrored() << ")";
      throw RegressionFailure(message.str());
    }
    Require(match->FirstTeam() == 1 && match->SecondTeam() == 0,
            "reverse_team_processing: processing order changed mid-run");
  }
  // Phase 2: reproducibility at env-step granularity.
  env.reset(config, false);
  Advance(env, 600);
  const SharedInfo reversed = env.get_info();
  const std::string reversed_digest = CaptureSimulationDigest(env);
  Require(reversed.is_in_play,
          "reverse_team_processing: the game never resumed");

  // Repeat from scratch. Unlike the plain reset check, the deep digest is
  // compared here too: this scenario reproduces exactly across resets, and a
  // digest is what catches damage the SharedInfo projection would hide.
  env.reset(config, false);
  Advance(env, 600);
  RequireInfoEqual(env.get_info(), reversed, "reverse_team_processing repeat");
  Require(CaptureSimulationDigest(env) == reversed_digest,
          "reverse_team_processing repeat: simulation digest differs");

  config.reverse_team_processing = saved_reverse;
}


// Formats a float as a valid C++ float literal, so that generated baseline
// entries can be pasted into the source without hand editing.
const char* FloatLiteral(float value) {
  if (value == 0.0f) return "0.0";  // avoid a noisy -0.0 in diffs
  static char buffers[9][64];
  static int next = 0;
  char* buffer = buffers[next];
  next = (next + 1) % 9;
  std::snprintf(buffer, 64, "%.9g", value);
  if (std::string(buffer).find_first_of(".eEnN") == std::string::npos) {
    std::strncat(buffer, ".0", 63 - std::strlen(buffer));
  }
  return buffer;
}

void PrintBaseline(GameEnv& env, ScenarioConfig& config) {
  const int calls[] = {1, 100, 500, 1000};
  env.reset(config, false);
  int completed = 0;
  std::cout << "  const GoldenSnapshot golden[] = {\n";
  for (const int target : calls) {
    Advance(env, target - completed);
    completed = target;
    const SharedInfo info = env.get_info();
    const Position& ball = info.ball_position;
    const Position& left = info.left_team.front().player_position;
    const Position& right = info.right_team.front().player_position;
    char line[1024];
    std::snprintf(
        line, sizeof(line),
        "      {%d, %d, Position(%sf, %sf, %sf, true),\n"
        "       Position(%sf, %sf, %sf, true),\n"
        "       Position(%sf, %sf, %sf, true), %d, %d, %s,\n"
        "       UINT64_C(%llu)},\n",
        target, info.step, FloatLiteral(ball.env_coord(0)),
        FloatLiteral(ball.env_coord(1)), FloatLiteral(ball.env_coord(2)),
        FloatLiteral(left.env_coord(0)), FloatLiteral(left.env_coord(1)),
        FloatLiteral(left.env_coord(2)), FloatLiteral(right.env_coord(0)),
        FloatLiteral(right.env_coord(1)), FloatLiteral(right.env_coord(2)),
        info.left_goals, info.right_goals,
        info.is_in_play ? "true" : "false",
        static_cast<unsigned long long>(HashInfo(info)));
    std::cout << line;
  }
  std::cout << "  };\n";
}

// The retained ball used to be positioned from a body-part transform read out
// of the skeletal animation tree, so its simulation position depended on when
// the presentation pipeline last refreshed that pose. H3a replaced that with a
// body-local anchor computed from simulation state.
//
// This checks the anchor semantics directly, then drives a real retain and
// requires the live ball to sit exactly on the computed anchor -- so the test
// cannot pass vacuously by never entering the retain path.
void CheckRetainAnchor(GameEnv& env, ScenarioConfig& config) {
  RetainAnchorKind kind = RetainAnchorKind::LeftElbow;
  Require(ParseRetainAnchorKind("right_elbow", kind) &&
              kind == RetainAnchorKind::RightElbow,
          "right_elbow must parse");
  Require(ParseRetainAnchorKind("left_elbow", kind) &&
              kind == RetainAnchorKind::LeftElbow,
          "left_elbow must parse");
  // Unknown anchors must not fall back silently: the legacy code asserted.
  Require(!ParseRetainAnchorKind("", kind), "empty retain state must not parse");
  Require(!ParseRetainAnchorKind("left_hand", kind),
          "unknown retain state must not parse");

  const Vector3 position(3.0f, -4.0f, 0.0f);
  const Vector3 forward(0.0f, -1.0f, 0.0f);
  const Vector3 rightAnchor =
      ComputeRetainAnchor(position, forward, RetainAnchorKind::RightElbow);
  const Vector3 leftAnchor =
      ComputeRetainAnchor(position, forward, RetainAnchorKind::LeftElbow);
  RequireNear(rightAnchor.coords[1], position.coords[1],
              "retain anchor should not push the ball forward");
  RequireNear(rightAnchor.coords[2], 1.26f, "retain anchor height");
  RequireNear(leftAnchor.coords[2], 1.26f, "retain anchor height (left)");
  Require(rightAnchor.coords[0] < position.coords[0] &&
              leftAnchor.coords[0] > position.coords[0],
          "left and right elbows must sit on opposite sides of the body");

  env.reset(config, false);
  for (int i = 0; i < 40; ++i) env.step();

  Match* match = env.context->gameTask->GetMatch();
  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  Require(!players.empty(), "retain scenario: no active player");
  Player* retainer = players.front();

  // The retain chain can be left by the controller's own animation selection,
  // so the retaining animation is re-asserted each tick to keep the path live.
  bool anchorObserved = false;
  for (int i = 0; i < 60; ++i) {
    retainer->SelectRetainAnim();
    match->SetBallRetainer(retainer);
    env.step();
    const Vector3 expectedAnchor =
        ComputeRetainAnchor(retainer->GetPosition(),
                            retainer->GetBodyDirectionVec(),
                            RetainAnchorKind::RightElbow);
    if (match->GetBall()->Predict(0).GetDistance(expectedAnchor) < 1e-3f) {
      anchorObserved = true;
    }
  }
  Require(anchorObserved,
          "retain scenario: the retain anchor path never ran, the test would "
          "be vacuous");
}


int main(int argc, char** argv) {
  if (!std::getenv("GFOOTBALL_DATA_DIR")) {
    std::cerr << "Set GFOOTBALL_DATA_DIR before running football_regression.\n";
    return 2;
  }

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
    GameEnv env;
    env.game_config.render = false;
    env.start_game();
    ScenarioConfig config = MakeBuiltinAiConfig();

    // Regenerate the golden baseline deliberately and reproducibly instead of
    // hand-editing the snapshot table. Run with --print-baseline and paste the
    // output over the `golden` array in CheckGoldenSnapshots.
    if (argc > 1 && std::string(argv[1]) == "--print-baseline") {
      PrintBaseline(env, config);
      return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "--animation-ab" ||
                     std::string(argv[1]) == "--animation-ab-lifecycle")) {
      CheckMovementAnimationPerturbation(
          env, config, std::string(argv[1]) == "--animation-ab-lifecycle");
      std::cout << "football_regression: PASS (animation A/B observation)\n";
      return 0;
    }

    // Officials are placed in the Officials constructor, before any simulation
    // tick runs. Check the mirror here so that a placement path that bypasses
    // the synchronized reset is caught (the first Process would silently heal
    // it later).
    CheckKinematicMirrorConsistency(env, "kinematic mirror after start_game");
    CheckResetBitDeterminism(env, config);

    CheckGoldenSnapshots(env, config);
    CheckImportHierarchy();
    CheckRetainAnchor(env, config);
    CheckResetAndStateRoundTrip(env, config);
    CheckDecisionContinuityRestoreDeterminism(env, config);
    CheckMovementAuthorityTiming(env, config);
    MeasureProceduralLocomotionDivergence(env, config);
    MeasureLocomotionRegimeTransitions(env, config);
    MeasureLegacyBodyFacing(env, config);
    MeasureBodyFacingShadowGrid(env, config);
    MeasureMovementCommandLifecycle(env, config);
    MeasureLocomotionPrediction(env, config);
    MeasureInterceptPrediction(env, config);
    MeasureHybridInterceptApproximation(env, config);
    CheckMatchTransitions(env, config);
    CheckReachabilityCadence(env, config);
    CheckReverseTeamProcessing(env, config);
    MeasureContactAuthority(env, config);
    std::cout << "football_regression: PASS\n";
    return 0;
  } catch (const RegressionFailure& failure) {
    std::cerr << "football_regression: FAIL: " << failure.what() << '\n';
    return 1;
  }
}
