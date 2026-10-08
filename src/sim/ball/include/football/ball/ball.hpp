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
  // Instantaneous linear impulse: dv = J / mass.
  void ApplyImpulse(const Vector3& impulse);
  // Contact-point impulse. Linear part is applied in the cohesion phase;
  // angular coupling (dw = I^-1 (r x J)) is deferred to the physics upgrade.
  void ApplyImpulseAtPoint(const Vector3& impulse, const Vector3& world_point);

  // ---- Stable simulation ----
  void Step(football::sim::TickSpan dt,
            const football::ball::BallEnvironment& environment);
  // Read-only trajectory prediction sharing the same physics kernel as Step.
  football::ball::BallState Predict(
      football::sim::TickSpan ahead,
      const football::ball::BallEnvironment& environment) const;

  // ---- Stable lifecycle and coordinate frame ----
  void Reset(const football::ball::BallState& state);
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
  Quaternion GetOrientation() const { return orientation_; }

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
  void Process(const football::ball::BallEnvironment& environment);
  void ResetSituation(const Vector3& focusPos);

 private:
  football::ball::detail::PhysicsState Snapshot() const;
  void Commit(const football::ball::detail::PhysicsState& state);
  void RefreshPredictions(const football::ball::BallEnvironment& environment);

  football::ball::BallConfig config_;
  Vector3 momentum_;
  Quaternion rotation_ms_;
  Vector3 position_;
  Quaternion orientation_;
  const football::model::Pitch pitch_;
  std::unique_ptr<football::ball::detail::BallPredictionCache> prediction_cache_;
  Vector3 pending_force_;
};

namespace football::ball {
// Stable namespaced identity for the Ball runtime type. Consumers address the
// module as football::ball::Ball; the global class name above is transitional.
using ::Ball;
}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_HPP