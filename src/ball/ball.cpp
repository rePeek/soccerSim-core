#include "football/ball/ball.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ball_netting.hpp"
#include "football/ball/ball_response.hpp"
#include "football/foundation/math/scalar.hpp"
#include "football/foundation/time/tick.hpp"

using namespace blunted;

namespace football::ball {
using football::geometry::Capsule;
using football::geometry::Collider;
using football::geometry::ColliderId;
using football::geometry::ColliderMotion;
using football::geometry::ColliderShape;
using football::geometry::ContactMaterial;
using football::geometry::Plane;
using football::geometry::Sphere;

namespace {

// The Magnus chain is wired through BallDynamics::magnus_coefficient (rad/s x
// m/s -> m/s^2). The value below is an explicit, deterministic starting point
// for the new unit system; it is not a bit-match of the historical swerve
// formula and is recalibrated by the trajectory-diff step of the upgrade.
constexpr float kMagnusCoefficient = 0.02f;

BallDynamics BuildDynamics(const football::ball::BallModel& config,
                           const GroundSurface& ground) {
  BallDynamics dynamics;
  dynamics.gravity = 9.81f;
  dynamics.quadratic_resistance = config.drag();
  dynamics.ground_deceleration = ground.deceleration;
  dynamics.quadratic_ground_resistance = ground.quadratic_resistance;
  dynamics.grass_height = ground.grass_height;
  dynamics.spin_decay = 0.0f;
  dynamics.magnus_coefficient = kMagnusCoefficient;
  return dynamics;
}

}  // namespace

Ball::Ball(const football::ball::BallModel& config, BallWorld world)
    : config_(config),
      world_(std::move(world)),
      state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()},
      dynamics_(BuildDynamics(config, world_.ground)) {
  tick_colliders_.reserve(73);
}

Ball::~Ball() = default;

BallState Ball::state() const { return state_; }

BallStepResult Ball::AdvanceState(const BallState& current,
                                  std::span<const ColliderMotion> colliders,
                                  const BallEnvironment& environment) const {
  BallStepResult result = AdvanceBallTick(
      current, colliders, football::sim::kTickSeconds, config_, dynamics_);
  // Flexible netting stays a discrete correction, not a rigid collider.
  detail::ResolveNetting(result.state.position, result.state.velocity, world_.net,
                         config_, environment);
  return result;
}



BallStepResult Ball::EvaluateTick(const BallTickInput& input,
                                  std::vector<ColliderMotion>& scratch) const {
  // Validation precedes all state/force consumption in both preview and commit.
  if (input.active_impulse && input.endpoint_constraint)
    throw std::invalid_argument("active impulse and endpoint constraint conflict");
  for (std::size_t i = 0; i < input.dynamic_colliders.size(); ++i) {
    const auto id = input.dynamic_colliders[i].id;
    if (id == 0 || std::any_of(world_.static_colliders.begin(), world_.static_colliders.end(),
                             [id](const auto& c) { return c.id == id; }))
      throw std::invalid_argument("dynamic collider id conflicts with pitch or is zero");
    for (std::size_t j = 0; j < i; ++j) {
      if (input.dynamic_colliders[j].id == id)
        throw std::invalid_argument("duplicate dynamic collider id");
    }
  }
  std::span<const ColliderMotion> all_colliders = world_.static_colliders;
  if (!input.dynamic_colliders.empty()) {
    scratch.assign(world_.static_colliders.begin(), world_.static_colliders.end());
    scratch.insert(scratch.end(), input.dynamic_colliders.begin(),
                   input.dynamic_colliders.end());
    all_colliders = scratch;
  }
  BallState initial = state_;
  BallStepResult result = AdvanceState(initial, all_colliders, input.environment);
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
    result.endpoint_constraint = input.endpoint_constraint;
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
  return result;
}


BallState Ball::Predict(football::sim::TickSpan ahead,
                        const BallEnvironment& environment) const {
  BallState predicted = state_;
  for (football::sim::TickSpan i{0}; i < ahead; i += football::sim::TickSpan{1}) {
    predicted = AdvanceState(predicted, world_.static_colliders, environment).state;
  }
  return predicted;
}

void Ball::Reset(const BallState& state) {
  state_ = state;
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
}



}  // namespace football::ball
