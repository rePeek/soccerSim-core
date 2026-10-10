#include "football/ball/ball.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ball_contact.hpp"
#include "ball_prediction.hpp"
#include "football/ball/ball_response.hpp"
#include "foundation/math/scalar.hpp"
#include "foundation/time/tick.hpp"

using namespace blunted;

namespace football::ball {

namespace {

// Legacy per-millisecond rotation quaternion projection (kept only for the
// transitional BallSpatialInfo / CalculatePrediction adapter).
Quaternion RotationMsFromAngularVelocity(const Vector3& angular_velocity) {
  Quaternion rotation;
  rotation.SetAngles(angular_velocity.coords[0] * 0.001f,
                     angular_velocity.coords[1] * 0.001f,
                     angular_velocity.coords[2] * 0.001f);
  return rotation;
}

// The Magnus chain is wired through BallDynamics::magnus_coefficient (rad/s x
// m/s -> m/s^2). The value below is an explicit, deterministic starting point
// for the new unit system; it is NOT a bit-match of the legacy swerve formula
// and is recalibrated by the trajectory-diff step of the physics upgrade.
constexpr float kMagnusCoefficient = 0.02f;

BallDynamics BuildDynamics(const football::model::BallConfig& config,
                           const football::model::Pitch& pitch) {
  BallDynamics dynamics;
  dynamics.gravity = 9.81f;
  dynamics.quadratic_resistance = config.drag();
  dynamics.ground_deceleration = pitch.ground_deceleration();
  dynamics.quadratic_ground_resistance = pitch.quadratic_resistance();
  dynamics.grass_height = pitch.grass_height();
  dynamics.spin_decay = 0.0f;
  dynamics.magnus_coefficient = kMagnusCoefficient;
  return dynamics;
}

}  // namespace

Ball::Ball(const football::model::BallConfig& config,
           const football::model::Pitch& pitch)
    : config_(config),
      pitch_(pitch),
      state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()},
      dynamics_(BuildDynamics(config, pitch)),
      colliders_(BuildPitchColliders(pitch, config)),
      prediction_cache_(std::make_unique<detail::BallPredictionCache>()),
      pending_force_(Vector3(0)) {
  RefreshPredictions(BallEnvironment{});
}

Ball::Ball(const football::model::Pitch& pitch)
    : Ball(football::model::BallConfig{}, pitch) {}

Ball::~Ball() = default;

BallState Ball::state() const { return state_; }

void Ball::RefreshPredictions(const BallEnvironment& environment) {
  prediction_cache_->Compute(state_, config_, pitch_, colliders_, dynamics_,
                             environment);
}

BallState Ball::AdvanceState(const BallState& current,
                             const BallEnvironment& environment) const {
  BallStepResult result = AdvanceBallTick(
      current, colliders_, football::sim::kTickSeconds, config_, dynamics_);
  // Transitional goal-netting correction. The static pitch world owns the
  // posts/crossbar; the netting stays a discrete correction until the goal
  // frame is expressed as full collider geometry (P7).
  detail::ResolveNetting(result.state.position, result.state.velocity, pitch_,
                         config_, environment);
  return result.state;
}

void Ball::AdvanceOneTick(const BallEnvironment& environment) {
  state_ = AdvanceState(state_, environment);
}

void Ball::ApplyForce(const Vector3& force) { pending_force_ += force; }

void Ball::ApplyImpulse(const Vector3& impulse) {
  state_.velocity += impulse / config_.mass();
  prediction_cache_->Invalidate();
  RefreshPredictions(BallEnvironment{});
}

void Ball::ApplyImpulseAtPoint(const Vector3& impulse,
                               const Vector3& world_point) {
  state_ = football::ball::ApplyImpulseAtPoint(state_, impulse, world_point,
                                               config_);
  prediction_cache_->Invalidate();
  RefreshPredictions(BallEnvironment{});
}

void Ball::Step(football::sim::TickSpan dt, const BallEnvironment& environment) {
  // Zero ticks must not silently drop an accumulated force.
  if (dt == football::sim::TickSpan{0}) return;

  if (pending_force_ != Vector3(0)) {
    state_.velocity +=
        (pending_force_ / config_.mass()) * football::sim::ToSeconds(dt);
    pending_force_ = Vector3(0);
  }

  for (football::sim::TickSpan i{0}; i < dt; i += football::sim::TickSpan{1}) {
    AdvanceOneTick(environment);
  }

  // The real state is authoritative; the cache is rebuilt, never read, by Step.
  prediction_cache_->Invalidate();
  RefreshPredictions(BallEnvironment{});
}

BallState Ball::Predict(football::sim::TickSpan ahead,
                        const BallEnvironment& environment) const {
  BallState predicted = state_;
  for (football::sim::TickSpan i{0}; i < ahead; i += football::sim::TickSpan{1}) {
    predicted = AdvanceState(predicted, environment);
  }
  return predicted;
}

void Ball::Reset(const BallState& state) {
  state_ = state;
  pending_force_ = Vector3(0);
  prediction_cache_->Invalidate();
  RefreshPredictions(BallEnvironment{});
}

void Ball::Mirror() {
  state_.position.Mirror();
  state_.velocity.Mirror();
  // Angular velocity is an axial vector: a 180 degree rotation about z flips
  // the x and y components and leaves z unchanged (Vector3::Mirror does exactly
  // that). Orientation gains the corresponding 180 degree z rotation.
  state_.angular_velocity.Mirror();
  const Quaternion half_turn(0.0f, 0.0f, 1.0f, 0.0f);
  state_.orientation = (half_turn * state_.orientation).GetNormalized();
  prediction_cache_->Invalidate();
  RefreshPredictions(BallEnvironment{});
}

Vector3 Ball::Predict(football::sim::TickSpan horizon) const {
  return prediction_cache_->Sample(horizon);
}

Vector3 Ball::Predict(int predictTime_ms) const {
  return Predict(football::sim::TickSpan{static_cast<std::uint64_t>(
      std::max(predictTime_ms, 0)) / football::sim::kMillisecondsPerTick});
}

void Ball::GetPredictionArray(std::vector<Vector3>& target) const {
  prediction_cache_->CopyTo(target);
}

Vector3 Ball::GetMovement() const { return state_.velocity; }

Vector3 Ball::GetRotation() const { return state_.angular_velocity * 0.001f; }

Quaternion Ball::GetOrientation() const { return state_.orientation; }

void Ball::Touch(const Vector3& target, const BallEnvironment& environment) {
  prediction_cache_->Invalidate();
  if (state_.position.coords[2] < config_.radius()) {
    state_.position.coords[2] = config_.radius();
  }
  SetMomentum(target, environment);
  CalculatePrediction(environment);
}

void Ball::SetPosition(const Vector3& target, const BallEnvironment& environment) {
  prediction_cache_->Invalidate();
  state_.position = target;
  state_.velocity = Vector3(0);
  state_.angular_velocity = Vector3(0);
  state_.orientation = Quaternion();
  RefreshPredictions(environment);
}

void Ball::SetMomentum(const Vector3& target, const BallEnvironment& environment) {
  state_.velocity = target;
  prediction_cache_->Invalidate();
  RefreshPredictions(environment);
}

void Ball::SetRotation(real x, real y, real z, float bias,
                       const BallEnvironment& environment) {
  // Legacy callers pass radians/second per axis; the transitional path blends
  // angular velocity directly instead of slerping the per-millisecond rotation
  // quaternion.
  const Vector3 target(x, y, z);
  state_.angular_velocity =
      state_.angular_velocity * (1.0f - bias) + target * bias;
  prediction_cache_->Invalidate();
  RefreshPredictions(environment);
}

BallSpatialInfo Ball::CalculatePrediction(const BallEnvironment& environment) {
  prediction_cache_->Compute(state_, config_, pitch_, colliders_, dynamics_,
                             environment);
  return BallSpatialInfo(state_.velocity,
                         RotationMsFromAngularVelocity(state_.angular_velocity));
}

void Ball::Process(const BallEnvironment& environment) {
  Step(football::sim::TickSpan{1}, environment);
}

void Ball::ResetSituation(const Vector3& focusPos) {
  state_.velocity = Vector3(0);
  state_.angular_velocity = Vector3(0);
  state_.orientation = Quaternion();
  state_.position = Vector3(focusPos + Vector3(0, 0, config_.radius()));
  pending_force_ = Vector3(0);
  prediction_cache_->Reset(state_.position);
}

}  // namespace football::ball
