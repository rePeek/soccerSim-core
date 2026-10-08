#ifndef FOOTBALL_BALL_BALL_HPP
#define FOOTBALL_BALL_BALL_HPP

// Public contract of the independent football::ball module.
//
// Design boundary:
//   include/football/ball/*  - what Simulation/Player/Referee/Observation may
//                              consume; no private physics header may be included
//   src/                     - private physics and prediction implementation
//
// R4a-1: the runtime class is defined inside namespace football::ball. There is
// no global `Ball` name and no `using namespace` in this public header.

#include <memory>
#include <vector>

#include "football/ball/ball_config.hpp"
#include "football/ball/ball_environment.hpp"
#include "football/ball/ball_state.hpp"
#include "football/ball/ball_timing.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"
#include "model/pitch.hpp"

namespace football::ball {

struct BallSpatialInfo {
  // Transitional result shape for the legacy re-calculation adapter.
  BallSpatialInfo(const blunted::Vector3& momentum,
                  const blunted::Quaternion& rotation_ms) {
    this->momentum = momentum;
    this->rotation_ms = rotation_ms;
  }
  blunted::Vector3 momentum;
  blunted::Quaternion rotation_ms;
};

namespace detail {
class BallPredictionCache;
struct PhysicsState;
}  // namespace detail

class Ball {
 public:
  explicit Ball(const BallConfig& config,
                const football::model::Pitch& pitch);
  explicit Ball(const football::model::Pitch& pitch);
  ~Ball();

  // ---- Stable state observation ----
  BallState state() const;

  // ---- Stable external physical input ----
  // Continuous force accumulated for the next Step interval.
  void ApplyForce(const blunted::Vector3& force);
  // Instantaneous linear impulse: dv = J / mass. Refreshes the transitional
  // prediction cache so a following legacy Predict() cannot observe stale data.
  // The cache refresh assumes a neutral environment; goal-aware prediction must
  // use Predict(ahead, environment).
  void ApplyImpulse(const blunted::Vector3& impulse);
  // Transitional contact-point impulse. Phase 1 applies only the linear part;
  // the angular coupling (dw = I^-1 (r x J)) is deliberately deferred to the
  // physics-upgrade phase, so this is NOT yet a complete contact contract.
  void ApplyImpulseAtPoint(const blunted::Vector3& impulse,
                           const blunted::Vector3& world_point);

  // ---- Stable simulation ----
  // Advances `dt` fixed simulation ticks, each kTickSeconds long. dt == 0 is a
  // no-op: it consumes no accumulated force and leaves predictions untouched.
  // This is the single real-motion entry point; the transitional Process()
  // forwards here.
  void Step(football::sim::TickSpan dt,
            const BallEnvironment& environment);
  // Read-only trajectory prediction sharing the same physics kernel as Step.
  // It never mutates the real state and does not use the legacy cache.
  BallState Predict(football::sim::TickSpan ahead,
                    const BallEnvironment& environment) const;

  // ---- Stable lifecycle and coordinate frame ----
  // Explicit state replacement. Predictions are rebuilt with a neutral
  // environment; use Predict(ahead, environment) when goal geometry matters.
  void Reset(const BallState& state);
  // Legacy coordinate mirror: flips position, momentum and the prediction
  // cache. Angular velocity and orientation are intentionally not mirrored,
  // matching the historical implementation, so this is not a full physical
  // state mirror yet.
  void Mirror();

  // ---- Transitional compatibility API (do not use in new code) ----
  // Legacy position-only prediction and re-calculation adapter. These carry
  // the historical cache semantics and are removed once consumers converge on
  // Predict()/Step()/state()/Reset().
  blunted::Vector3 Predict(football::sim::TickSpan horizon) const;
  blunted::Vector3 Predict(int predictTime_ms) const;
  void GetPredictionArray(std::vector<blunted::Vector3>& target) const;
  blunted::Vector3 GetMovement() const;
  blunted::Vector3 GetRotation() const;
  blunted::Quaternion GetOrientation() const;

  // Absolute-velocity touch / state mutation, not a physical impulse. Keep the
  // legacy semantics exactly through the cohesion phase; new callers must use
  // ApplyImpulse/ApplyImpulseAtPoint/Reset.
  void Touch(const blunted::Vector3& target,
             const BallEnvironment& environment);
  void SetPosition(const blunted::Vector3& target,
                   const BallEnvironment& environment);
  void SetMomentum(const blunted::Vector3& target,
                   const BallEnvironment& environment);
  // Radians per second for each axis (legacy per-millisecond storage).
  void SetRotation(blunted::real x, blunted::real y, blunted::real z, float bias,
                   const BallEnvironment& environment);
  BallSpatialInfo CalculatePrediction(const BallEnvironment& environment);
  // Transitional single-tick adapter; equivalent to Step(TickSpan{1}, env).
  void Process(const BallEnvironment& environment);
  void ResetSituation(const blunted::Vector3& focusPos);

 private:
  void Commit(const detail::PhysicsState& state);
  // One legacy-quantum tick: publishes the cache from the pre-tick state, then
  // commits the first predicted state. Shared by Step() and Process().
  void AdvanceOneTick(const BallEnvironment& environment);
  void RefreshPredictions(const BallEnvironment& environment);

  BallConfig config_;
  const football::model::Pitch pitch_;
  // Single authoritative internal state (public BallState is a projection).
  // It stays a private detail type because the legacy kernel needs the full
  // per-millisecond rotation quaternion, which a 3-DOF rad/s angular velocity
  // cannot represent losslessly. Unifying the two is part of the deferred
  // physics upgrade.
  std::unique_ptr<detail::PhysicsState> state_;
  std::unique_ptr<detail::BallPredictionCache> prediction_cache_;
  blunted::Vector3 pending_force_;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_HPP
