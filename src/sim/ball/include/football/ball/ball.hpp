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

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_environment.hpp"
#include "football/ball/ball_state.hpp"
#include "football/ball/ball_timing.hpp"
#include "football/ball/collider.hpp"
#include "football/ball/pitch_colliders.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"
#include "model/ball_config.hpp"
#include "model/pitch.hpp"

namespace football::ball {

// Borrowed input for exactly one 10ms interval, in BallState's coordinate frame.
// IDs must be nonzero, unique, and disjoint from pitch IDs 1--7. Shape motions
// are translation-only. No Player/rules identities or active impulses (P5).
struct BallTickInput {
  std::span<const ColliderMotion> dynamic_colliders;
  BallEnvironment environment;
};

// Transitional projection shape for the legacy re-calculation adapter.
struct BallSpatialInfo {
  BallSpatialInfo(const blunted::Vector3& velocity,
                  const blunted::Quaternion& rotation_ms) {
    this->momentum = velocity;
    this->rotation_ms = rotation_ms;
  }
  blunted::Vector3 momentum;      // actually velocity (m/s)
  blunted::Quaternion rotation_ms;  // legacy per-millisecond rotation quaternion
};

namespace detail {
class BallPredictionCache;
}  // namespace detail

class Ball {
 public:
  explicit Ball(const football::model::BallConfig& config,
                const football::model::Pitch& pitch);
  explicit Ball(const football::model::Pitch& pitch);
  ~Ball();

  // ---- Stable state observation ----
  BallState state() const;

  // ---- Stable external physical input ----
  // Force accumulated for the next Step interval. It is applied as one
  // velocity change dv = (F / mass) * ToSeconds(dt) before that Step
  // integrates, i.e. it models a constant force across the whole interval.
  void ApplyForce(const blunted::Vector3& force);
  // Instantaneous linear impulse: dv = J / mass.
  void ApplyImpulse(const blunted::Vector3& impulse);
  // Contact-point impulse: linear and angular velocity change through the
  // unified point-impulse response (ApplyImpulseAtPoint in ball_response.hpp).
  void ApplyImpulseAtPoint(const blunted::Vector3& impulse,
                           const blunted::Vector3& world_point);

  // ---- Stable simulation ----
  // Exactly one tick, at most one passive impact, no remainder integration.
  // Returns physical evidence only; overlap/resting contact is not a rule touch.
  BallStepResult Step(const BallTickInput& input);
  // Transitional static-only duration adapter. Zero ticks is a no-op; legacy
  // accumulated-force semantics for multi-tick calls are retained until P7.
  void Step(football::sim::TickSpan dt, const BallEnvironment& environment);
  // Read-only trajectory prediction sharing the same physics kernel as Step.
  BallState Predict(football::sim::TickSpan ahead,
                    const BallEnvironment& environment) const;

  // ---- Stable lifecycle and coordinate frame ----
  // Explicit state replacement. Predictions are rebuilt with a neutral
  // environment; use Predict(ahead, environment) when goal geometry matters.
  void Reset(const BallState& state);
  // Coordinate mirror: flips position, velocity, angular velocity and
  // orientation, then rebuilds the prediction cache. This is a full physical
  // state mirror (180 degree rotation about z), unlike the legacy
  // position/momentum-only mirror.
  void Mirror();

  // ---- Transitional compatibility API (do not use in new code) ----
  blunted::Vector3 Predict(football::sim::TickSpan horizon) const;
  blunted::Vector3 Predict(int predictTime_ms) const;
  void GetPredictionArray(std::vector<blunted::Vector3>& target) const;
  blunted::Vector3 GetMovement() const;
  blunted::Vector3 GetRotation() const;
  blunted::Quaternion GetOrientation() const;

  // Absolute-velocity touch / state mutation, not a physical impulse.
  void Touch(const blunted::Vector3& target,
             const BallEnvironment& environment);
  void SetPosition(const blunted::Vector3& target,
                   const BallEnvironment& environment);
  void SetMomentum(const blunted::Vector3& target,
                   const BallEnvironment& environment);
  // Radians per second for each axis.
  void SetRotation(blunted::real x, blunted::real y, blunted::real z, float bias,
                   const BallEnvironment& environment);
  BallSpatialInfo CalculatePrediction(const BallEnvironment& environment);
  // Transitional single-tick adapter; equivalent to Step(TickSpan{1}, env).
  void Process(const BallEnvironment& environment);
  void ResetSituation(const blunted::Vector3& focusPos);

 private:
  // Pure tick computation including the transitional flexible net correction.
  BallStepResult AdvanceState(const BallState& current,
                              std::span<const ColliderMotion> colliders,
                              const BallEnvironment& environment) const;
  void RefreshPredictions(const BallEnvironment& environment);

  football::model::BallConfig config_;
  const football::model::Pitch pitch_;
  // Single authoritative internal state (the public BallState projection is a
  // straight copy, not a lossy conversion).
  BallState state_;
  BallDynamics dynamics_;
  std::vector<ColliderMotion> colliders_;
  // Reused merge scratch; 7 static + 22*3 body motions fit without growth.
  // Larger rosters may grow it once. Prediction never consumes this buffer.
  std::vector<ColliderMotion> tick_colliders_;
  std::unique_ptr<detail::BallPredictionCache> prediction_cache_;
  blunted::Vector3 pending_force_;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_HPP
