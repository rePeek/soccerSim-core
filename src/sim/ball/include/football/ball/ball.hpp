#ifndef FOOTBALL_BALL_BALL_HPP
#define FOOTBALL_BALL_BALL_HPP

// Public contract of the independent football::ball module.
//
// Design boundary:
//   include/football/ball/*  - what Simulation/Player/Referee/Observation may
//                              consume; no private physics header may be included
//   src/                     - private physics and prediction implementation
//
// Transitional note: the concrete runtime class still lives in the global
// namespace to match the rest of the legacy runtime (Player/Team/Referee) and
// all existing call sites. `football::ball::Ball` is provided as the stable,
// namespaced alias. A later isolation pass may move the class body into the
// namespace and drop the global name.

#include <memory>
#include <vector>

#include "football/ball/ball_config.hpp"
#include "football/ball/ball_environment.hpp"
#include "football/ball/ball_state.hpp"
#include "football/ball/ball_timing.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"
#include "model/pitch.hpp"

using namespace blunted;

struct BallSpatialInfo {
  // Transitional result shape for the legacy re-calculation adapter.
  BallSpatialInfo(const Vector3 &momentum, const Quaternion &rotation_ms) {
    this->momentum = momentum;
    this->rotation_ms = rotation_ms;
  }
  Vector3 momentum;
  Quaternion rotation_ms;
};

namespace football::ball::detail {
class BallPredictionCache;
struct PhysicsState;
}  // namespace football::ball::detail

// See namespace football::ball for the stable public type definitions and the
// namespaced alias below.
class Ball {
 public:
  explicit Ball(const football::ball::BallConfig& config,
                const football::model::Pitch& pitch);
  explicit Ball(const football::model::Pitch& pitch);
  ~Ball();

  // ---- Stable state observation ----
  football::ball::BallState state() const;

  // ---- Stable external physical input ----
  // Continuous force accumulated for the next Step interval.
  void ApplyForce(const Vector3& force);
  // Instantaneous linear impulse: dv = J / mass. Refreshes the transitional
  // prediction cache so a following legacy Predict() cannot observe stale data.
  // The cache refresh assumes a neutral environment; goal-aware prediction must
  // use Predict(ahead, environment).
  void ApplyImpulse(const Vector3& impulse);
  // Transitional contact-point impulse. Phase 1 applies only the linear part;
  // the angular coupling (dw = I^-1 (r x J)) is deliberately deferred to the
  // physics-upgrade phase, so this is NOT yet a complete contact contract.
  void ApplyImpulseAtPoint(const Vector3& impulse, const Vector3& world_point);

  // ---- Stable simulation ----
  // Advances `dt` fixed simulation ticks, each kTickSeconds long. dt == 0 is a
  // no-op: it consumes no accumulated force and leaves predictions untouched.
  // This is the single real-motion entry point; the transitional Process()
  // forwards here.
  void Step(football::sim::TickSpan dt,
            const football::ball::BallEnvironment& environment);
  // Read-only trajectory prediction sharing the same physics kernel as Step.
  // It never mutates the real state and does not use the legacy cache.
  football::ball::BallState Predict(
      football::sim::TickSpan ahead,
      const football::ball::BallEnvironment& environment) const;

  // ---- Stable lifecycle and coordinate frame ----
  // Explicit state replacement. Predictions are rebuilt with a neutral
  // environment; use Predict(ahead, environment) when goal geometry matters.
  void Reset(const football::ball::BallState& state);
  // Legacy coordinate mirror: flips position, momentum and the prediction
  // cache. Angular velocity and orientation are intentionally not mirrored,
  // matching the historical implementation, so this is not a full physical
  // state mirror yet.
  void Mirror();

  // ---- Transitional compatibility API (do not use in new code) ----
  // Legacy position-only prediction and re-calculation adapter. These carry
  // the historical cache semantics and are removed once consumers converge on
  // Predict()/Step()/state()/Reset().
  Vector3 Predict(football::sim::TickSpan horizon) const;
  Vector3 Predict(int predictTime_ms) const;
  void GetPredictionArray(std::vector<Vector3>& target) const;
  Vector3 GetMovement() const;
  Vector3 GetRotation() const;
  Quaternion GetOrientation() const;

  // Absolute-velocity touch / state mutation, not a physical impulse. Keep the
  // legacy semantics exactly through the cohesion phase; new callers must use
  // ApplyImpulse/ApplyImpulseAtPoint/Reset.
  void Touch(const Vector3& target,
             const football::ball::BallEnvironment& environment);
  void SetPosition(const Vector3& target,
                   const football::ball::BallEnvironment& environment);
  void SetMomentum(const Vector3& target,
                   const football::ball::BallEnvironment& environment);
  // Radians per second for each axis (legacy per-millisecond storage).
  void SetRotation(real x, real y, real z, float bias,
                   const football::ball::BallEnvironment& environment);
  BallSpatialInfo CalculatePrediction(
      const football::ball::BallEnvironment& environment);
  // Transitional single-tick adapter; equivalent to Step(TickSpan{1}, env).
  void Process(const football::ball::BallEnvironment& environment);
  void ResetSituation(const Vector3& focusPos);

 private:
  void Commit(const football::ball::detail::PhysicsState& state);
  // One legacy-quantum tick: publishes the cache from the pre-tick state, then
  // commits the first predicted state. Shared by Step() and Process().
  void AdvanceOneTick(const football::ball::BallEnvironment& environment);
  void RefreshPredictions(const football::ball::BallEnvironment& environment);

  football::ball::BallConfig config_;
  const football::model::Pitch pitch_;
  // Single authoritative internal state (public BallState is a projection).
  // It stays a private detail type because the legacy kernel needs the full
  // per-millisecond rotation quaternion, which a 3-DOF rad/s angular velocity
  // cannot represent losslessly. Unifying the two is part of the deferred
  // physics upgrade.
  std::unique_ptr<football::ball::detail::PhysicsState> state_;
  std::unique_ptr<football::ball::detail::BallPredictionCache> prediction_cache_;
  Vector3 pending_force_;
};

namespace football::ball {
// Stable namespaced identity for the Ball runtime type. Consumers address the
// module as football::ball::Ball; the global class name above is transitional.
using ::Ball;
}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_HPP