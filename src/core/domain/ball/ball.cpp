#include "ball.hpp"

namespace football::domain {

Ball::Ball(const BallProfile& profile, BallState& state)
    : profile_(profile), state_(state) {}

void Ball::SetRotation(blunted::real x, blunted::real y, blunted::real z,
                       float bias) {
  // radians per second for each axis
  const blunted::Vector3 target(x, y, z);
  state_.angularVelocity =
      state_.angularVelocity * (1.0f - bias) + target * bias;
}

void Ball::Reset(const blunted::Vector3& focusPos) {
  state_.velocity = blunted::Vector3(0);
  state_.angularVelocity = blunted::Vector3(0);
  state_.position =
      blunted::Vector3(focusPos + blunted::Vector3(0, 0, profile_.radius));
  state_.orientation = blunted::Quaternion(blunted::QUATERNION_IDENTITY);
}

}  // namespace football::domain