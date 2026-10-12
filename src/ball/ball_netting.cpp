#include "ball_netting.hpp"

#include <cmath>

#include "football/foundation/math/scalar.hpp"
#include "football/foundation/time/tick.hpp"

using namespace football::math;

namespace football::ball::detail {

void ResolveNetting(Vector3& nextPos, Vector3& momentumPredict,
                    const football::pitch::Pitch& pitch,
                    const football::ball::BallModel& config) {
  const float pitchHalfW = pitch.half_length();
  const float goalHalfWidth = pitch.goal_half_width();
  const float goalHeight = pitch.goal_height();
  const float goalDepth = pitch.goal_depth();
  const float ballRadius = config.radius();
  const float timeStep = football::sim::kTickSeconds;

  float netAbsorbInv = 0.95f;
  const float powFactor = 2.6f;
  const float powerFac = 1.8f;
  netAbsorbInv = std::pow(netAbsorbInv, timeStep * 100.0f);

  const bool behindBackline = std::fabs(nextPos.coords[0]) > pitchHalfW + ballRadius;
  const bool beforeGoalBack = std::fabs(nextPos.coords[0]) < pitchHalfW + goalDepth - ballRadius;
  const bool belowGoalHeight = nextPos.coords[2] < goalHeight + ballRadius;
  const bool betweenGoalWidth = std::fabs(nextPos.coords[1]) < goalHalfWidth - ballRadius;

  // side netting
  if ((!betweenGoalWidth && behindBackline)) {
    float netDist = 0.0f;
    netDist = std::fabs(fabs(nextPos.coords[1]) - goalHalfWidth);
    netDist = clamp(netDist, 0, 1);
    float power = std::pow(netDist, powFactor) *
                  -signSide(nextPos.coords[1]);

    // net is stuck to woodwork so lay off there
    float woodworkTensionBiasInv = clamp((std::fabs(momentumPredict.coords[0]) - pitchHalfW) * 2.0f, 0.0f, 1.0f);
    float adaptedPowerFac = powerFac + (1.0f - woodworkTensionBiasInv) * 3.0f;

    momentumPredict.coords[1] = momentumPredict.coords[1] * netAbsorbInv + power * adaptedPowerFac * (100 * timeStep);
  }

  // rear netting
  if ((!beforeGoalBack && behindBackline)) {
    float netDist = 0.0f;
    netDist = std::fabs(fabs(nextPos.coords[0]) - (pitchHalfW + goalDepth));
    netDist = clamp(netDist, 0, 1);
    float power = std::pow(netDist, powFactor) *
                  -signSide(nextPos.coords[0]);
    momentumPredict.coords[0] = momentumPredict.coords[0] * netAbsorbInv + power * powerFac * (100 * timeStep);
  }

  // top netting
  if ((!belowGoalHeight && behindBackline)) {
    float netDist = 0.0f;
    netDist = std::fabs(fabs(nextPos.coords[2]) - goalHeight);
    netDist = clamp(netDist, 0, 1);
    float power = std::pow(netDist, powFactor) * -1.0f;

    // net is stuck to woodwork so lay off there
    float woodworkTensionBiasInv = clamp((std::fabs(momentumPredict.coords[0]) - pitchHalfW) * 2.0f, 0.0f, 1.0f);
    float adaptedPowerFac = powerFac + (1.0f - woodworkTensionBiasInv) * 3.0f;

    momentumPredict.coords[2] = momentumPredict.coords[2] * netAbsorbInv + power * adaptedPowerFac * (100 * timeStep);
  }
}

}  // namespace football::ball::detail
