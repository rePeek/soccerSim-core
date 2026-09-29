#include "ball.hpp"

namespace football::model {

Ball::Ball(const BallProfile& profile, BallState& state)
    : profile_(profile), state_(state) {}

void Ball::SetRotation(blunted::real x, blunted::real y, blunted::real z,
                       float bias) {
  // Legacy axis convention: x is the forward-roll axis and maps to spin around
  // -X, while y and z map to +Y/+Z. Preserved so topspin/backspin keep their
  // direction after the Quaternion -> Vector3 angularVelocity migration.
  const blunted::Vector3 target(-x, y, z);
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

}  // namespace football::model