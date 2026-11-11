#include "football/ball/ball.hpp"

#include <format>

#include "football/ball/ball_contact.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/log/log.hpp"
#include "football/pitch/pitch.hpp"

using namespace football::log;
using namespace football::math;

namespace football::ball {

Ball::Ball(const BallModel& model)
    : model_(model), state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {
  Info(Channel::ball,
       std::format("ball created: mass={} radius={} magnus={}", model_.mass,
                   model_.radius, model_.magnus_coefficient));
}

BallStepResult Ball::EvaluateTick(const Pitch& pitch) const {
  return AdvanceBallTick(
      state_, pitch.colliders(), football::sim::kTickSeconds, model_, pitch);
}

BallStepResult Ball::Predict(const Pitch& pitch) const {
  return EvaluateTick(pitch);
}

BallStepResult Ball::Step(const Pitch& pitch) {
  auto result = EvaluateTick(pitch);
  state_ = result.state;
  if (!result.contacts.empty()) {
    Debug(Channel::ball, std::format("tick resolved {} contact(s); position=({}, {}, {})",
                                     result.contacts.size(), state_.position.coords[0],
                                     state_.position.coords[1], state_.position.coords[2]));
  }
  return result;
}

BallState Ball::Predict(const Pitch& pitch,
                        football::sim::TickSpan ahead) const {
  BallState predicted = state_;
  for (football::sim::TickSpan i{0}; i < ahead; i += football::sim::TickSpan{1}) {
    predicted = AdvanceBallTick(
        predicted, pitch.colliders(), football::sim::kTickSeconds, model_, pitch).state;
  }
  return predicted;
}

void Ball::PlaceOnGround(float x, float y) {
  BallState state;
  state.position = football::math::Vector3(x, y, model_.radius);
  set_state(state);
  Info(Channel::ball, std::format("placed on ground at ({}, {})", x, y));
}

}  // namespace football::ball
