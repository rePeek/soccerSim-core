#include "football/ball/ball.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "football/ball/ball_response.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"

using namespace football::math;

namespace football::ball {
using football::geometry::ColliderMotion;


Ball::Ball(const football::ball::BallModel& config,
         const football::pitch::Pitch& pitch)
    : config_(config),
      pitch_(pitch),
      state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {
  tick_colliders_.reserve(73);
}

Ball::~Ball() = default;

BallState Ball::state() const { return state_; }

BallStepResult Ball::AdvanceState(const BallState& current,
                                  std::span<const ColliderMotion> colliders) const {
  return AdvanceBallTick(
      current, colliders, football::sim::kTickSeconds, config_, pitch_);
}



BallStepResult Ball::EvaluateTick(const BallTickInput& input,
                                  std::vector<ColliderMotion>& scratch) const {
  if (input.active_impulse && input.endpoint_constraint)
    throw std::invalid_argument("active impulse and endpoint constraint conflict");
  for (std::size_t i = 0; i < input.dynamic_colliders.size(); ++i) {
    const auto id = input.dynamic_colliders[i].id;
    if (id == 0 || std::any_of(pitch_.colliders().begin(), pitch_.colliders().end(),
                             [id](const auto& c) { return c.id == id; }))
      throw std::invalid_argument("dynamic collider id conflicts with pitch or is zero");
    for (std::size_t j = 0; j < i; ++j) {
      if (input.dynamic_colliders[j].id == id)
        throw std::invalid_argument("duplicate dynamic collider id");
    }
  }
  std::span<const ColliderMotion> all_colliders = pitch_.colliders();
  if (!input.dynamic_colliders.empty()) {
    scratch.assign(pitch_.colliders().begin(), pitch_.colliders().end());
    scratch.insert(scratch.end(), input.dynamic_colliders.begin(),
                   input.dynamic_colliders.end());
    all_colliders = scratch;
  }
  BallState initial = state_;
  BallStepResult result = AdvanceState(initial, all_colliders);
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


BallState Ball::Predict(football::sim::TickSpan ahead) const {
  BallState predicted = state_;
  for (football::sim::TickSpan i{0}; i < ahead; i += football::sim::TickSpan{1}) {
    predicted = AdvanceState(predicted, pitch_.colliders()).state;
  }
  return predicted;
}

void Ball::Reset(const BallState& state) {
  state_ = state;
}

void Ball::Mirror() {
  state_.position.Mirror();
  state_.velocity.Mirror();
  state_.angular_velocity.Mirror();
  const Quaternion half_turn(0.0f, 0.0f, 1.0f, 0.0f);
  state_.orientation = (half_turn * state_.orientation).GetNormalized();
}



}
