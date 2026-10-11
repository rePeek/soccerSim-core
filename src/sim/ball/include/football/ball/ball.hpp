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
#include <optional>
#include <vector>

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_environment.hpp"
#include "football/ball/ball_state.hpp"
#include "football/ball/collider.hpp"
#include "football/ball/ball_world.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/time/tick.hpp"
#include "foundation/math/vector3.hpp"
#include "model/ball_config.hpp"

namespace football::ball {


// Borrowed input for exactly one 10ms interval, in BallState's coordinate frame.
// IDs must be nonzero, unique, and disjoint from pitch IDs 1--7.
struct BallTickInput {
  std::span<const ColliderMotion> dynamic_colliders;
  BallEnvironment environment;
  // P5b: at most one active impulse, supplied by Simulation after arbitration
  // and applied at the tick endpoint (after passive motion and netting). It
  // changes velocity and spin only; position is never advanced a second time
  // and no second Step is performed.
  std::optional<BallImpulse> active_impulse;
  // Mutually exclusive with active_impulse (e.g. constrained possession).
  std::optional<BallEndpointConstraint> endpoint_constraint;
};


class Ball {
 public:
  explicit Ball(const football::model::BallConfig& config, BallWorld world);
  ~Ball();

  // ---- Stable state observation ----
  BallState state() const;
  // Physics configuration actually in force (mass, radius, restitution).
  // Read-only; never a live channel for mutating the ball.
  const football::model::BallConfig& config() const { return config_; }

  // ---- Stable simulation ----
  // Exactly one tick, at most one passive impact, no remainder integration.
  // Returns physical evidence only; overlap/resting contact is not a rule touch.
  BallStepResult Step(const BallTickInput& input);
  // Read-only trajectory prediction sharing the same physics kernel as Step.
  BallState Predict(football::sim::TickSpan ahead,
                    const BallEnvironment& environment) const;
  // Read-only preview of precisely the same tick input/kernel as Step, including
  // dynamic CCD. Does not consume accumulated forces or modify any Ball state.
  BallStepResult Predict(const BallTickInput& input) const;

  // ---- Stable lifecycle and coordinate frame ----
  // Explicit state replacement. Predictions are rebuilt with a neutral
  // environment; use Predict(ahead, environment) when goal geometry matters.
  void Reset(const BallState& state);
  // Coordinate mirror: flips position, velocity, angular velocity and
  // orientation (a 180 degree rotation about z).
  void Mirror();

  // ---- Lifecycle state reset ----
  void ResetSituation(const blunted::Vector3& focusPos);

 private:
  // Pure tick computation including the flexible net correction.
  BallStepResult AdvanceState(const BallState& current,
                              std::span<const ColliderMotion> colliders,
                              const BallEnvironment& environment) const;
  BallStepResult EvaluateTick(const BallTickInput& input,
                              std::vector<ColliderMotion>& scratch) const;

  football::model::BallConfig config_;
  BallWorld world_;
  // Single authoritative internal state (the public BallState projection is a
  // straight copy, not a lossy conversion).
  BallState state_;
  BallDynamics dynamics_;
  std::vector<ColliderMotion> colliders_;
  // Reused merge scratch; 7 static + 22*3 body motions fit without growth.
  // Larger rosters may grow it once. Prediction never consumes this buffer.
  std::vector<ColliderMotion> tick_colliders_;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_HPP
