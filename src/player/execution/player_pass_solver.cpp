#include "football/player/execution/player_pass_solver.hpp"

#include <algorithm>
#include <cmath>

#include "football/foundation/math/scalar.hpp"

namespace football::sim::player {
namespace {

using football::math::clamp;
using football::math::NormalizedClamp;
using football::math::Vector3;

void CalculatePassDirectionAndPower(e_FunctionType passType, const Vector3& vector,
                                    Vector3& direction, float& power) {
  float heightOffset = 0.11f;
  float powerFactor = 1.8f;
  float distanceExp = 1.4f;
  if (passType == e_FunctionType_HighPass) {
    heightOffset = 0.45f - NormalizedClamp(vector.GetLength(), 0.0f, 60.0f) * 0.15f;
    powerFactor = 1.15f;
    distanceExp = 1.4f;
  }
  direction =
      (vector.GetNormalized(0) + Vector3(0, 0, heightOffset)).GetNormalized(0);
  power = std::pow(NormalizedClamp(vector.GetLength(), 0.0f, 60.0f),
                   distanceExp) *
          powerFactor;
}

}  // namespace

PassSolverResult SolvePass(const PassSolverInput& input) {
  PassSolverResult result;
  const PassSolverOrigin& origin = input.origin;
  Vector3 playerPos =
      origin.position + origin.movement.GetNormalized(0) * 0.2f;
  if (origin.has_touch_position) playerPos = origin.touch_position.Get2D();

  Vector3 target;
  if (input.target_player) {
    const float passDuration =
        std::pow(clamp(0.3f + (input.target_player->position - playerPos)
                                    .GetLength() *
                                    0.05f,
                       0.0f, 1.0f),
                 0.7f) *
        0.7f;
    target = input.target_player->position +
             input.target_player->movement * passDuration;
    result.targets_player = true;
  } else if (input.target_position) {
    target = *input.target_position;
  } else {
    return result;
  }

  Vector3 targetRel = target - playerPos;
  if (input.pass_type == e_FunctionType_LongPass) {
    targetRel += Vector3(
        -static_cast<float>(origin.defending_direction) *
            targetRel.GetLength() * 0.2f,
        0, 0);
  }
  CalculatePassDirectionAndPower(input.pass_type, targetRel, result.direction,
                                 result.power);
  result.solved = true;
  return result;
}

}  // namespace football::sim::player
