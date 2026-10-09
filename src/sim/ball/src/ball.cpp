#include "football/ball/ball.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ball_physics.hpp"
#include "ball_prediction.hpp"
#include "foundation/math/scalar.hpp"

using namespace blunted;

namespace football::ball {

namespace {

// Default physical frame: at rest at the origin, no spin, identity orientation.
detail::PhysicsState InitialState() {
  return detail::PhysicsState{Vector3(0), Vector3(0), Quaternion(), Quaternion()};
}

}  // namespace

Ball::Ball(const football::model::BallConfig& config,
    const football::model::Pitch& pitch)
    : config_(config),
      pitch_(pitch),
      state_(std::make_unique<detail::PhysicsState>(InitialState())),
      prediction_cache_(std::make_unique<detail::BallPredictionCache>()),
      pending_force_(Vector3(0)) {
  RefreshPredictions(BallEnvironment{});
}

Ball::Ball(const football::model::Pitch& pitch)
    : Ball(football::model::BallConfig{}, pitch) {}

Ball::~Ball() = default;

void Ball::Commit(const detail::PhysicsState& state) {
  *state_ = state;
}

void Ball::RefreshPredictions(const BallEnvironment& environment) {
  detail::PredictionResult ignored;
  prediction_cache_->Compute(*state_, config_, pitch_, environment, ignored);
}

BallState Ball::state() const {
  BallState result;
  result.position = state_->position;
  result.velocity = state_->momentum;
  real x, y, z;
  state_->rotation_ms.GetAngles(x, y, z);
  result.angular_velocity = Vector3(x * 1000.0f, y * 1000.0f, z * 1000.0f);
  result.orientation = state_->orientation;
  return result;
}

void Ball::ApplyForce(const Vector3& force) {
  pending_force_ += force;
}

void Ball::ApplyImpulse(const Vector3& impulse) {
  state_->momentum += impulse / config_.mass();
  // Rebuild, not only invalidate: the transitional cache-backed Predict()
  // reads the array directly, so it must not observe the pre-impulse path.
  prediction_cache_->Invalidate();
  RefreshPredictions(BallEnvironment{});
}

void Ball::ApplyImpulseAtPoint(const Vector3& impulse, const Vector3& world_point) {
  ApplyImpulse(impulse);
  // TODO(physics-upgrade): add the angular coupling dw = I^-1 (r x J) once the
  // module's physical behavior is allowed to change. Phase 1 keeps the legacy
  // absolute-velocity semantics intact.
  (void)world_point;
}

void Ball::AdvanceOneTick(const BallEnvironment& environment) {
  detail::PredictionResult result;
  prediction_cache_->Compute(*state_, config_, pitch_, environment, result);
  Commit(result.step_one);
}

void Ball::Step(football::sim::TickSpan dt, const BallEnvironment& environment) {
  // Zero ticks must not silently drop an accumulated force.
  if (dt == football::sim::TickSpan{0}) return;

  if (pending_force_ != Vector3(0)) {
    state_->momentum += (pending_force_ / config_.mass()) * football::sim::ToSeconds(dt);
    pending_force_ = Vector3(0);
  }

  for (football::sim::TickSpan i{0}; i < dt; i += football::sim::TickSpan{1}) {
    AdvanceOneTick(environment);
  }
}

BallState Ball::Predict(football::sim::TickSpan ahead,
                        const BallEnvironment& environment) const {
  detail::PhysicsState state = *state_;
  for (football::sim::TickSpan i{0}; i < ahead; i += football::sim::TickSpan{1}) {
    state = detail::Advance(state, config_, pitch_, environment,
                            /*first_step=*/ i == football::sim::TickSpan{0});
  }

  BallState result;
  result.position = state.position;
  result.velocity = state.momentum;
  real x, y, z;
  state.rotation_ms.GetAngles(x, y, z);
  result.angular_velocity = Vector3(x * 1000.0f, y * 1000.0f, z * 1000.0f);
  result.orientation = state.orientation;
  return result;
}

void Ball::Reset(const BallState& state) {
  state_->position = state.position;
  state_->momentum = state.velocity;
  Quaternion rotation;
  rotation.SetAngles(state.angular_velocity.coords[0] * 0.001f,
                     state.angular_velocity.coords[1] * 0.001f,
                     state.angular_velocity.coords[2] * 0.001f);
  state_->rotation_ms = rotation;
  state_->orientation = state.orientation;
  pending_force_ = Vector3(0);
  RefreshPredictions(BallEnvironment{});
}

void Ball::Mirror() {
  state_->momentum.Mirror();
  prediction_cache_->Mirror();
  state_->position.Mirror();
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

Vector3 Ball::GetMovement() const {
  return state_->momentum;
}

Vector3 Ball::GetRotation() const {
  real x, y, z;
  state_->rotation_ms.GetAngles(x, y, z);
  return Vector3(x, y, z);
}

Quaternion Ball::GetOrientation() const {
  return state_->orientation;
}

void Ball::Touch(const Vector3& target, const BallEnvironment& environment) {
  prediction_cache_->Invalidate();
  if (state_->position.coords[2] < config_.radius()) {
    state_->position.coords[2] = config_.radius();
  }

  SetMomentum(target, environment);

  // Preserve both historical recalculations; no cross-domain notifications here.
  CalculatePrediction(environment);
}

void Ball::SetPosition(const Vector3& target, const BallEnvironment& environment) {
  prediction_cache_->Invalidate();
  state_->position.Set(target);
  state_->momentum.Set(0);
  SetRotation(0, 0, 0, 1.0, environment);
}

void Ball::SetMomentum(const Vector3& target, const BallEnvironment& environment) {
  state_->momentum.Set(target);
  CalculatePrediction(environment);
}

void Ball::SetRotation(real x, real y, real z, float bias,
                       const BallEnvironment& environment) {
  // radians per second for each axis
  Quaternion rotX;
  rotX.SetAngleAxis(clamp(x * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(-1, 0, 0));
  Quaternion rotY;
  rotY.SetAngleAxis(clamp(y * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(0, 1, 0));
  Quaternion rotZ;
  rotZ.SetAngleAxis(clamp(z * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(0, 0, 1));

  Quaternion tmpRotation_ms = rotX * rotY * rotZ;
  state_->rotation_ms = state_->rotation_ms.GetSlerped(bias, tmpRotation_ms);

  CalculatePrediction(environment);
}

BallSpatialInfo Ball::CalculatePrediction(const BallEnvironment& environment) {
  detail::PredictionResult result;
  prediction_cache_->Compute(*state_, config_, pitch_, environment, result);
  return BallSpatialInfo(result.step_one.momentum, result.step_one.rotation_ms);
}

void Ball::Process(const BallEnvironment& environment) {
  // Single real-motion path: Process is exactly one Step tick.
  Step(football::sim::TickSpan{1}, environment);
}

void Ball::ResetSituation(const Vector3& focusPos) {
  state_->momentum = Vector3(0);
  state_->rotation_ms = QUATERNION_IDENTITY;
  prediction_cache_->Reset(focusPos + Vector3(0, 0, 0.11));
  state_->position = Vector3(focusPos + Vector3(0, 0, 0.11));
  state_->orientation = QUATERNION_IDENTITY;
  pending_force_ = Vector3(0);
}

}  // namespace football::ball
