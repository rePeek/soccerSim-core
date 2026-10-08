#include "football/ball/ball.hpp"

#include <algorithm>
#include <cmath>

#include "ball_physics.hpp"
#include "ball_prediction.hpp"
#include "foundation/math/scalar.hpp"

namespace {

namespace detail = football::ball::detail;

using football::ball::BallConfig;
using football::ball::BallEnvironment;
using football::ball::BallState;
using football::ball::detail::PhysicsState;

}  // namespace

Ball::Ball(const BallConfig& config, const football::model::Pitch& pitch)
    : config_(config),
      momentum_(Vector3(0)),
      rotation_ms_(Quaternion()),
      position_(Vector3(0)),
      orientation_(Quaternion()),
      pitch_(pitch),
      prediction_cache_(std::make_unique<detail::BallPredictionCache>()),
      pending_force_(Vector3(0)) {
  RefreshPredictions(BallEnvironment{});
}

Ball::Ball(const football::model::Pitch& pitch) : Ball(BallConfig{}, pitch) {}

Ball::~Ball() = default;

PhysicsState Ball::Snapshot() const {
  return PhysicsState{position_, momentum_, rotation_ms_, orientation_};
}

void Ball::Commit(const PhysicsState& state) {
  position_ = state.position;
  momentum_ = state.momentum;
  rotation_ms_ = state.rotation_ms;
  orientation_ = state.orientation;
}

void Ball::RefreshPredictions(const BallEnvironment& environment) {
  detail::PredictionResult ignored;
  prediction_cache_->Compute(Snapshot(), config_, pitch_, environment, ignored);
}

BallState Ball::state() const {
  BallState result;
  result.position = position_;
  result.velocity = momentum_;
  real x, y, z;
  rotation_ms_.GetAngles(x, y, z);
  result.angular_velocity = Vector3(x * 1000.0f, y * 1000.0f, z * 1000.0f);
  result.orientation = orientation_;
  return result;
}

void Ball::ApplyForce(const Vector3& force) {
  pending_force_ += force;
}

void Ball::ApplyImpulse(const Vector3& impulse) {
  momentum_ += impulse / config_.mass;
  prediction_cache_->Invalidate();
}

void Ball::ApplyImpulseAtPoint(const Vector3& impulse, const Vector3& world_point) {
  ApplyImpulse(impulse);
  // TODO(physics-upgrade): add the angular coupling dw = I^-1 (r x J) once the
  // module's physical behavior is allowed to change. Phase 1 keeps the legacy
  // absolute-velocity semantics intact.
  (void)world_point;
}

void Ball::Step(football::sim::TickSpan dt, const BallEnvironment& environment) {
  if (pending_force_ != Vector3(0)) {
    const float seconds = football::sim::ToSeconds(dt);
    momentum_ += (pending_force_ / config_.mass) * seconds;
    pending_force_ = Vector3(0);
  }

  PhysicsState state = Snapshot();
  for (football::sim::TickSpan i{0}; i < dt; i += football::sim::TickSpan{1}) {
    state = detail::Advance(state, config_, pitch_, environment,
                            /*first_step=*/ i == football::sim::TickSpan{0});
  }
  Commit(state);
  RefreshPredictions(environment);
}

BallState Ball::Predict(football::sim::TickSpan ahead,
                        const BallEnvironment& environment) const {
  PhysicsState state = Snapshot();
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
  position_ = state.position;
  momentum_ = state.velocity;
  Quaternion rotation;
  rotation.SetAngles(state.angular_velocity.coords[0] * 0.001f,
                     state.angular_velocity.coords[1] * 0.001f,
                     state.angular_velocity.coords[2] * 0.001f);
  rotation_ms_ = rotation;
  orientation_ = state.orientation;
  pending_force_ = Vector3(0);
  RefreshPredictions(BallEnvironment{});
}

void Ball::Mirror() {
  momentum_.Mirror();
  prediction_cache_->Mirror();
  position_.Mirror();
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
  return momentum_;
}

Vector3 Ball::GetRotation() const {
  real x, y, z;
  rotation_ms_.GetAngles(x, y, z);
  return Vector3(x, y, z);
}

void Ball::Touch(const Vector3& target, const BallEnvironment& environment) {
  prediction_cache_->Invalidate();
  if (position_.coords[2] < config_.radius) position_.coords[2] = config_.radius;

  SetMomentum(target, environment);

  // Preserve both historical recalculations; no cross-domain notifications here.
  CalculatePrediction(environment);
}

void Ball::SetPosition(const Vector3& target, const BallEnvironment& environment) {
  prediction_cache_->Invalidate();
  position_.Set(target);
  momentum_.Set(0);
  SetRotation(0, 0, 0, 1.0, environment);
}

void Ball::SetMomentum(const Vector3& target, const BallEnvironment& environment) {
  momentum_.Set(target);
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
  rotation_ms_ = rotation_ms_.GetSlerped(bias, tmpRotation_ms);

  CalculatePrediction(environment);
}

BallSpatialInfo Ball::CalculatePrediction(const BallEnvironment& environment) {
  detail::PredictionResult result;
  prediction_cache_->Compute(Snapshot(), config_, pitch_, environment, result);
  return BallSpatialInfo(result.step_one.momentum, result.step_one.rotation_ms);
}

void Ball::Process(const BallEnvironment& environment) {
  detail::PredictionResult result;
  prediction_cache_->Compute(Snapshot(), config_, pitch_, environment, result);
  Commit(result.step_one);
}

void Ball::ResetSituation(const Vector3& focusPos) {
  momentum_ = Vector3(0);
  rotation_ms_ = QUATERNION_IDENTITY;
  prediction_cache_->Reset(focusPos + Vector3(0, 0, 0.11));
  position_ = Vector3(focusPos + Vector3(0, 0, 0.11));
  orientation_ = QUATERNION_IDENTITY;
  pending_force_ = Vector3(0);
}
