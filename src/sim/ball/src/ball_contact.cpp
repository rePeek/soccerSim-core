#include "ball_contact.hpp"

#include <cmath>

#include "foundation/math/scalar.hpp"
#include "sim/time/tick.hpp"


using namespace blunted;

namespace football::ball::detail {

namespace {
constexpr float kPostRadius = 0.07f;
constexpr float kPostAbsorbInv = 0.8f;
}  // namespace

void ResolveWoodwork(Vector3& nextPos, Vector3& momentumPredict,
                     const football::model::Pitch& pitch,
                     const football::ball::BallConfig& config) {
  const float pitchHalfW = pitch.half_length();
  const float goalHalfWidth = pitch.goal_half_width();
  const float goalHeight = pitch.goal_height();

  const float ballRadius = config.radius;

  // posts
  if (nextPos.coords[2] < goalHeight + ballRadius + kPostRadius &&
      (nextPos.Get2D().GetAbsolute() -
       Vector3(pitchHalfW, goalHalfWidth, 0))
              .GetLength() < ballRadius + kPostRadius) {
    Vector3 normal;

    if (nextPos.coords[0] < 0) {
      // left side of pitch
      if (nextPos.coords[1] < 0) {
        // 'lower' side of pitch
        normal = (nextPos.Get2D() - Vector3(-pitchHalfW, -goalHalfWidth, 0)).GetNormalized(Vector3(1, 0, 0));
        float nextPosZ = nextPos.coords[2];
        nextPos = Vector3(-pitchHalfW, -goalHalfWidth, 0) + normal * (kPostRadius + ballRadius);
        nextPos.coords[2] = nextPosZ;
      } else {
        // 'upper' side of pitch
        normal = (nextPos.Get2D() - Vector3(-pitchHalfW, goalHalfWidth, 0)).GetNormalized(Vector3(1, 0, 0));
        float nextPosZ = nextPos.coords[2];
        nextPos = Vector3(-pitchHalfW, goalHalfWidth, 0) + normal * (kPostRadius + ballRadius);
        nextPos.coords[2] = nextPosZ;
      }
    } else {
      // right side of pitch
      if (nextPos.coords[1] < 0) {
        // 'lower' side of pitch
        normal = (nextPos.Get2D() - Vector3(pitchHalfW, -goalHalfWidth, 0)).GetNormalized(Vector3(-1, 0, 0));
        float nextPosZ = nextPos.coords[2];
        nextPos = Vector3(pitchHalfW, -goalHalfWidth, 0) + normal * (kPostRadius + ballRadius);
        nextPos.coords[2] = nextPosZ;
      } else {
        // 'upper' side of pitch
        normal = (nextPos.Get2D() - Vector3(pitchHalfW, goalHalfWidth, 0)).GetNormalized(Vector3(-1, 0, 0));
        float nextPosZ = nextPos.coords[2];
        nextPos = Vector3(pitchHalfW, goalHalfWidth, 0) + normal * (kPostRadius + ballRadius);
        nextPos.coords[2] = nextPosZ;
      }
    }

    momentumPredict = (momentumPredict.Get2D().GetNormalized(normal) + (normal * 1.1f)).GetNormalized() * momentumPredict.Get2D().GetLength() * kPostAbsorbInv + (Vector3(0, 0, 1) * momentumPredict.coords[2]);
  }

  // crossbar
  Vector3 nextPosXZ = nextPos * Vector3(1, 0, 1);
  if ((nextPosXZ.GetAbsolute() - Vector3(pitchHalfW, 0, goalHeight))
              .GetLength() < ballRadius + kPostRadius &&
      std::fabs(nextPos.coords[1]) < goalHalfWidth + ballRadius + kPostRadius) {
    Vector3 normal;

    if (nextPos.coords[0] < 0) {
      // left side of pitch
      normal = (nextPosXZ - Vector3(-pitchHalfW, 0, goalHeight)).GetNormalized(Vector3(0, 0, 1));
      float nextPosY = nextPos.coords[1];
      nextPos = Vector3(-pitchHalfW, 0, goalHeight) + normal * (kPostRadius + ballRadius);
      nextPos.coords[1] = nextPosY;
    } else {
      // right side of pitch
      normal = (nextPosXZ - Vector3(pitchHalfW, 0, goalHeight)).GetNormalized(Vector3(0, 0, -1));
      float nextPosY = nextPos.coords[1];
      nextPos = Vector3(pitchHalfW, 0, goalHeight) + normal * (kPostRadius + ballRadius);
      nextPos.coords[1] = nextPosY;
    }

    Vector3 momentumPredictXZ = momentumPredict * Vector3(1, 0, 1);
    momentumPredict = (momentumPredictXZ.GetNormalized(normal) + (normal * 1.1f)).GetNormalized() * momentumPredictXZ.GetLength() * kPostAbsorbInv + (Vector3(0, 1, 0) * momentumPredict.coords[1]);
  }
}

void ResolveNetting(Vector3& nextPos, Vector3& momentumPredict,
                    const football::model::Pitch& pitch,
                    const football::ball::BallConfig& config,
                    const football::ball::BallEnvironment& environment) {
  const float pitchHalfW = pitch.half_length();
  const float goalHalfWidth = pitch.goal_half_width();
  const float goalHeight = pitch.goal_height();
  const float goalDepth = pitch.goal_depth();
  const float ballRadius = config.radius;
  const float timeStep = football::sim::kTickSeconds;

  float netAbsorbInv = 0.95f;
  const float powFactor = 2.6f;
  const float powerFac = 1.8f;
  netAbsorbInv = std::pow(netAbsorbInv, timeStep * 100.0f);

  const bool ballIsInGoal = environment.ball_in_goal;
  const signed int inGoal = ballIsInGoal ? 1 : -1;

  const bool behindBackline = std::fabs(nextPos.coords[0]) > pitchHalfW + ballRadius;
  const bool behindGoalBack = std::fabs(nextPos.coords[0]) > pitchHalfW + goalDepth + ballRadius;
  const bool beforeGoalBack = std::fabs(nextPos.coords[0]) < pitchHalfW + goalDepth - ballRadius;
  const bool belowGoalHeight = nextPos.coords[2] < goalHeight + ballRadius;
  const bool betweenGoalWidth = std::fabs(nextPos.coords[1]) < goalHalfWidth - ballRadius;

  // side netting
  if ((ballIsInGoal && !betweenGoalWidth && behindBackline)) {
    float netDist = 0.0f;
    netDist = std::fabs(fabs(nextPos.coords[1]) - goalHalfWidth);
    netDist = clamp(netDist, 0, 1);
    float power = std::pow(netDist, powFactor) *
                  -signSide(nextPos.coords[1]) * inGoal;

    // net is stuck to woodwork so lay off there
    float woodworkTensionBiasInv = clamp((std::fabs(momentumPredict.coords[0]) - pitchHalfW) * 2.0f, 0.0f, 1.0f);
    float adaptedPowerFac = powerFac + (1.0f - woodworkTensionBiasInv) * 3.0f;

    momentumPredict.coords[1] = momentumPredict.coords[1] * netAbsorbInv + power * adaptedPowerFac * (100 * timeStep);
  }

  // rear netting
  if ((ballIsInGoal && !beforeGoalBack && behindBackline)) {
    float netDist = 0.0f;
    netDist = std::fabs(fabs(nextPos.coords[0]) - (pitchHalfW + goalDepth));
    netDist = clamp(netDist, 0, 1);
    float power = std::pow(netDist, powFactor) *
                  -signSide(nextPos.coords[0]) * inGoal;
    momentumPredict.coords[0] = momentumPredict.coords[0] * netAbsorbInv + power * powerFac * (100 * timeStep);
  }

  // top netting
  if ((ballIsInGoal && !belowGoalHeight && behindBackline)) {
    float netDist = 0.0f;
    netDist = std::fabs(fabs(nextPos.coords[2]) - goalHeight);
    netDist = clamp(netDist, 0, 1);
    float power = std::pow(netDist, powFactor) * -inGoal;

    // net is stuck to woodwork so lay off there
    float woodworkTensionBiasInv = clamp((std::fabs(momentumPredict.coords[0]) - pitchHalfW) * 2.0f, 0.0f, 1.0f);
    float adaptedPowerFac = powerFac + (1.0f - woodworkTensionBiasInv) * 3.0f;

    momentumPredict.coords[2] = momentumPredict.coords[2] * netAbsorbInv + power * adaptedPowerFac * (100 * timeStep);
  }
}

}  // namespace football::ball::detail