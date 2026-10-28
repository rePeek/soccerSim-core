#include "football/contact/passive_body_touch.hpp"

#include "football/foundation/math/scalar.hpp"

namespace football::contact {
using namespace football::math;

float PassiveBodyTouchRadius(e_FunctionType action, bool has_possession) {
  float boundingBoxSizeOffset = -0.1f;
  if (!has_possession) boundingBoxSizeOffset += 0.03f;
  else boundingBoxSizeOffset -= 0.03f;
  if (action == e_FunctionType_Sliding || action == e_FunctionType_Interfere)
    boundingBoxSizeOffset += 0.1f;
  if (action == e_FunctionType_Deflect) boundingBoxSizeOffset += 0.2f;
  return 0.11f + boundingBoxSizeOffset;
}

void AccumulatePassiveBodyMotion(PassiveBodyTouchAccumulator& accumulator,
    const Vector3& ball_position, const Vector3& volume_center,
    const Vector3& player_velocity, float opponent_touch_bias) {
  float movementBias = opponent_touch_bias * 0.8f + 0.2f;
  accumulator.bounce +=
      (ball_position - volume_center).GetNormalized(Vector3(0)) * movementBias +
      player_velocity * (1.0f - movementBias);
  accumulator.count++;
}

void AccumulatePassiveBodyBias(PassiveBodyTouchAccumulator& accumulator,
    const Vector3& ball_position, const Vector3& volume_center,
    float ball_radius, float volume_radius) {
  accumulator.bias += (1.0f -
      clamp(((ball_position - volume_center).GetLength() - ball_radius) /
                volume_radius, 0.0f, 1.0f)) * 0.9f + 0.1f;
}

PassiveBodyTouchResult FinishPassiveBodyTouches(
    PassiveBodyTouchAccumulator accumulator, const Vector3& ball_velocity) {
  auto& bounceVec = accumulator.bounce;
  auto& bias = accumulator.bias;
  if (!(bias > 0.0f)) return {};
  bounceVec /= (accumulator.count * 1.0f);
  bounceVec.coords[2] *= 0.6f;
  bounceVec.Normalize();
  Vector3 currentMovement = ball_velocity;
  Vector3 fullCollisionVec = (bounceVec * 6.0f) + (bounceVec * currentMovement.GetLength() * 0.6f) + (currentMovement * -0.2f);
  bias = clamp(bias, 0.0f, 1.0f);
  bias = bias * 0.5f + 0.5f;
  Vector3 resultVector = fullCollisionVec * bias + currentMovement * (1.0f - bias);
  if (resultVector.GetLength() > currentMovement.GetLength()) resultVector = resultVector.GetNormalized(0) * currentMovement.GetLength();
  resultVector *= 0.7f;
  return {resultVector, 0.5f * bias};
}

}  // namespace football::contact
