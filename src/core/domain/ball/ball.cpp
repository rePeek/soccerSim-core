#include "ball.hpp"

Ball::Ball(BallState& state) : state_(state) {}

void Ball::SetRotation(blunted::real x, blunted::real y, blunted::real z,
                       float bias) {
  // radians per second for each axis
  blunted::Quaternion rotX;
  rotX.SetAngleAxis(
      blunted::clamp(x * 0.001f, -blunted::pi * 0.49f, blunted::pi * 0.49f),
      blunted::Vector3(-1, 0, 0));
  blunted::Quaternion rotY;
  rotY.SetAngleAxis(
      blunted::clamp(y * 0.001f, -blunted::pi * 0.49f, blunted::pi * 0.49f),
      blunted::Vector3(0, 1, 0));
  blunted::Quaternion rotZ;
  rotZ.SetAngleAxis(
      blunted::clamp(z * 0.001f, -blunted::pi * 0.49f, blunted::pi * 0.49f),
      blunted::Vector3(0, 0, 1));

  blunted::Quaternion tmpRotation_ms = rotX * rotY * rotZ;
  state_.rotation_ms =
      state_.rotation_ms.GetSlerped(bias, tmpRotation_ms);
}

void Ball::Reset(const blunted::Vector3& focusPos) {
  state_.momentum = blunted::Vector3(0);
  state_.rotation_ms = blunted::Quaternion(blunted::QUATERNION_IDENTITY);
  state_.position = blunted::Vector3(focusPos + blunted::Vector3(0, 0, 0.11));
  state_.orientation = blunted::Quaternion(blunted::QUATERNION_IDENTITY);
}