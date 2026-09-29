#include "ball.hpp"

#include <cassert>

namespace football_sim {

Ball::Ball(float radius, float mass, float inertiaFactor)
    : radius_(radius), mass_(mass), inertiaFactor_(inertiaFactor) {
  assert(radius_ > 0.0f);
  assert(mass_ > 0.0f);
  assert(inertiaFactor_ > 0.0f);
  Reset(football_sim::math::Vector3(0.0f, 0.0f, 0.0f));
}

void Ball::Reset(const football_sim::math::Vector3& focusPosition) {
  BallState next;
  next.position = focusPosition + football_sim::math::Vector3(0.0f, 0.0f, radius_);
  next.orientation = football_sim::math::Quaternion(football_sim::math::QUATERNION_IDENTITY);
  SetState(next);
}

}  // namespace football_sim
