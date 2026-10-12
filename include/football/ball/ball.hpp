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

#include <optional>
#include <vector>

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_state.hpp"
#include "football/geometry/collider.hpp"
#include "football/pitch/pitch.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/ball/ball_model.hpp"

namespace football::ball {


// Borrowed input for exactly one 10ms interval, in BallState's coordinate frame.
// IDs must be nonzero, unique, and disjoint from pitch IDs 1--7.
struct BallTickInput {
  std::span<const football::geometry::ColliderMotion> dynamic_colliders;
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
  Ball(const football::ball::BallModel& model,
       const football::pitch::Pitch& pitch);
  ~Ball();

  // ---- Stable state observation ----
  BallState state() const;
  // Physics configuration actually in force (mass, radius, aerodynamics).
  // Read-only; never a live channel for mutating the ball.
  const football::ball::BallModel& config() const { return config_; }

  // ---- Stable simulation ----
  // Exactly one tick, at most one passive impact, no remainder integration.
  // Returns physical evidence only; overlap/resting contact is not a rule touch.
  BallStepResult Step(const BallTickInput& input);
  // Read-only trajectory prediction sharing the same physics kernel as Step.
  // The pitch (gravity, ground, static colliders) comes from the Ball itself.
  BallState Predict(football::sim::TickSpan ahead) const;
  // Read-only preview of precisely the same tick input/kernel as Step, including
  // dynamic CCD. Does not consume accumulated forces or modify any Ball state.
  BallStepResult Predict(const BallTickInput& input) const;

  // ---- Stable lifecycle and coordinate frame ----
  // Explicit state replacement (prediction shares the live state).
  // Goal geometry comes from the stored pitch.
  void Reset(const BallState& state);
  // Coordinate mirror: flips position, velocity, angular velocity and
  // orientation (a 180 degree rotation about z).
  void Mirror();


 private:
  // Pure tick computation including the flexible net correction.
  BallStepResult AdvanceState(
      const BallState& current,
      std::span<const football::geometry::ColliderMotion> colliders) const;
  BallStepResult EvaluateTick(const BallTickInput& input,
                              std::vector<football::geometry::ColliderMotion>& scratch) const;

  football::ball::BallModel config_;
  // The immutable pitch scene the ball moves in; must outlive this Ball.
  const football::pitch::Pitch& pitch_;
  // Single authoritative internal state (the public BallState projection is a
  // straight copy, not a lossy conversion).
  BallState state_;
  // Reused merge scratch; 7 static + 22*3 body motions fit without growth.
  // Larger rosters may grow it once. Prediction never consumes this buffer.
  std::vector<football::geometry::ColliderMotion> tick_colliders_;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_HPP
