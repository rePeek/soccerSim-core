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
  tick_colliders_.reserve(73);
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

BallStepResult Ball::AdvanceState(const BallState& current,
                                  std::span<const ColliderMotion> colliders,
                                  const BallEnvironment& environment) const {
  BallStepResult result = AdvanceBallTick(
      current, colliders, football::sim::kTickSeconds, config_, dynamics_);
  // Flexible netting stays a discrete correction, not a rigid collider.
  detail::ResolveNetting(result.state.position, result.state.velocity, pitch_,
                         config_, environment);
  return result;
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

void Ball::ApplyContactImpulse(const Vector3& impulse,
                               const Vector3& contact_point,
                               const BallEnvironment& environment) {
  // Match the legacy resting-height clamp so ball position is unchanged.
  if (state_.position.coords[2] < config_.radius()) {
    state_.position.coords[2] = config_.radius();
  }
  state_ = football::ball::ApplyImpulseAtPoint(state_, impulse, contact_point, config_);
  prediction_cache_->Invalidate();
  RefreshPredictions(environment);
}

BallStepResult Ball::EvaluateTick(const BallTickInput& input,
                                  std::vector<ColliderMotion>& scratch) const {
  // Validation precedes all state/force consumption in both preview and commit.
  if (input.active_impulse && input.endpoint_constraint)
    throw std::invalid_argument("active impulse and endpoint constraint conflict");
  for (std::size_t i = 0; i < input.dynamic_colliders.size(); ++i) {
    const auto id = input.dynamic_colliders[i].id;
    if (id == 0 || std::any_of(colliders_.begin(), colliders_.end(),
                             [id](const auto& c) { return c.id == id; }))
      throw std::invalid_argument("dynamic collider id conflicts with pitch or is zero");
    for (std::size_t j = 0; j < i; ++j) {
      if (input.dynamic_colliders[j].id == id)
        throw std::invalid_argument("duplicate dynamic collider id");
    }
  }
  std::span<const ColliderMotion> world = colliders_;
  if (!input.dynamic_colliders.empty()) {
    scratch.assign(colliders_.begin(), colliders_.end());
    scratch.insert(scratch.end(), input.dynamic_colliders.begin(),
                   input.dynamic_colliders.end());
    world = scratch;
  }
  BallState initial = state_;
  if (pending_force_ != Vector3(0)) {
    initial.velocity += (pending_force_ / config_.mass()) * football::sim::kTickSeconds;
  }
  BallStepResult result = AdvanceState(initial, world, input.environment);
  // P5b: the active impulse belongs to the tick endpoint. It is applied after
  // passive motion and netting, changes only velocity/spin about the given
  // point, never advances position again and never runs a second Step.
  if (input.active_impulse.has_value()) {
    result.state = football::ball::ApplyImpulseAtPoint(
        result.state, input.active_impulse->impulse,
        input.active_impulse->contact_point, config_);
    result.active_impulse = input.active_impulse;
  }
  if (input.endpoint_constraint) {
    result.state.position = input.endpoint_constraint->position;
    result.state.velocity = input.endpoint_constraint->velocity;
    result.state.angular_velocity = Vector3(0);
  }
  return result;
}

BallStepResult Ball::Predict(const BallTickInput& input) const {
  std::vector<ColliderMotion> scratch;
  return EvaluateTick(input, scratch);
}

BallStepResult Ball::Step(const BallTickInput& input) {
  auto result = EvaluateTick(input, tick_colliders_);
  state_ = result.state;
  pending_force_ = Vector3(0);
  prediction_cache_->Invalidate();
  RefreshPredictions(input.environment);
  return result;
}

void Ball::Step(football::sim::TickSpan dt, const BallEnvironment& environment) {
  // Zero ticks must not silently drop an accumulated force.
  if (dt == football::sim::TickSpan{0}) return;
  if (dt == football::sim::TickSpan{1}) {
    Step(BallTickInput{{}, environment});
    return;
  }

  if (pending_force_ != Vector3(0)) {
    state_.velocity +=
        (pending_force_ / config_.mass()) * football::sim::ToSeconds(dt);
    pending_force_ = Vector3(0);
  }

  for (football::sim::TickSpan i{0}; i < dt; i += football::sim::TickSpan{1}) {
    state_ = AdvanceState(state_, colliders_, environment).state;
  }

  // The real state is authoritative; the cache is rebuilt, never read, by Step.
  prediction_cache_->Invalidate();
  RefreshPredictions(BallEnvironment{});
}

BallState Ball::Predict(football::sim::TickSpan ahead,
                        const BallEnvironment& environment) const {
  BallState predicted = state_;
  for (football::sim::TickSpan i{0}; i < ahead; i += football::sim::TickSpan{1}) {
    predicted = AdvanceState(predicted, colliders_, environment).state;
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
