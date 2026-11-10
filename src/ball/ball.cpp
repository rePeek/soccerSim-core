#include "football/ball/ball.hpp"

#include "football/ball/ball_contact.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"

using namespace football::math;

namespace football::ball {

Ball::Ball(const football::ball::BallModel& config,
           const football::pitch::Pitch& pitch)
    : config_(config),
      pitch_(pitch),
      state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {}

BallState Ball::state() const { return state_; }

BallStepResult Ball::EvaluateTick() const {
  return AdvanceBallTick(
      state_, pitch_.colliders(), football::sim::kTickSeconds, config_, pitch_);
}

BallStepResult Ball::Predict() const { return EvaluateTick(); }

BallStepResult Ball::Step() {
  auto result = EvaluateTick();
  state_ = result.state;
  return result;
}

BallState Ball::Predict(football::sim::TickSpan ahead) const {
  BallState predicted = state_;
  for (football::sim::TickSpan i{0}; i < ahead; i += football::sim::TickSpan{1}) {
    predicted = AdvanceBallTick(
        predicted, pitch_.colliders(), football::sim::kTickSeconds, config_, pitch_).state;
  }
  return predicted;
}

void Ball::Reset(const BallState& state) { state_ = state; }

void Ball::PlaceOnGround(float x, float y) {
  BallState state;
  state.position = football::math::Vector3(x, y, config_.radius());
  Reset(state);
}

}  // namespace football::ball
