#include "ball.hpp"

#include <cassert>

namespace football::model {

Ball::Ball(float radius, float mass, float inertiaFactor)
    : radius_(radius), mass_(mass), inertiaFactor_(inertiaFactor) {
  assert(radius_ > 0.0f);
  assert(mass_ > 0.0f);
  assert(inertiaFactor_ > 0.0f);
  Reset(blunted::Vector3(0.0f, 0.0f, 0.0f));
}

void Ball::Reset(const blunted::Vector3& focusPosition) {
  BallState next;
  next.position = focusPosition + blunted::Vector3(0.0f, 0.0f, radius_);
  next.orientation = blunted::Quaternion(blunted::QUATERNION_IDENTITY);
  SetState(next);
}

}  // namespace football::model
