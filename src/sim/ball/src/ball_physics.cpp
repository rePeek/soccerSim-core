#include "ball_physics.hpp"

#include <cmath>

#include "ball_contact.hpp"
#include "foundation/math/scalar.hpp"

using namespace blunted;

namespace football::ball::detail {

namespace {
// Legacy empirical coefficients. Kept as literals on purpose: the cohesion
// phase must reproduce the historical kernel exactly. Ground-surface values
// now come from football::model::Pitch; BallConfig carries the ball's own
// radius/restitution/drag; the rest remain private tuning constants.
constexpr float kLinearBounce = 0.06f;
constexpr float kGravity = -9.81f;
}  // namespace

PhysicsState Advance(const PhysicsState& current,
                     const football::model::BallConfig& config,
                     const football::model::Pitch& pitch,
                     const football::ball::BallEnvironment& environment,
                     bool first_step) {
  Vector3 momentumPredict = current.momentum;
  Quaternion rotationPredict_ms = current.rotation_ms;
  Vector3 nextPos = current.position;
  Quaternion nextOrientation = current.orientation;

  constexpr bool drag_enabled = true;
  constexpr bool groundFriction_enabled = true;
  constexpr bool woodwork_enabled = true;
  constexpr bool netting_enabled = true;
  constexpr bool groundRotationEffects_enabled = true;
  constexpr bool swerve_enabled = true;
  constexpr float timeStep = football::sim::kTickSeconds;

  float frictionFactor = 0.0f;

  // gravity
  momentumPredict.coords[2] = momentumPredict.coords[2] + kGravity * timeStep;

  // air resistance
  float momentumVelo = momentumPredict.GetLength();
  float momentumVeloDragged =
      momentumVelo - config.drag() * std::pow(momentumVelo, 2.0f) * timeStep;
  if (drag_enabled) momentumPredict = momentumPredict.GetNormalized(0) * momentumVeloDragged;

  float ballBottom = nextPos.coords[2] - config.radius();
  float grassInfluenceBias = clamp(1.0f - (ballBottom / pitch.grass_height()), 0.0f, 1.0f);
  grassInfluenceBias = std::pow(grassInfluenceBias, 0.7f);

  // bounce
  if (nextPos.coords[2] < config.radius()) {
    if (momentumPredict.coords[2] < 0.0f) {
      frictionFactor = NormalizedClamp(-momentumPredict.coords[2] - 0.5f, 0.0f, 12.0f);
      momentumPredict.coords[2] = -momentumPredict.coords[2] * config.restitution();
      momentumPredict.coords[2] = std::max(momentumPredict.coords[2] - kLinearBounce, 0.0f);
    }
    nextPos.coords[2] = config.radius();
  }

  // ground friction
  if (nextPos.coords[2] < config.radius() + pitch.grass_height() && groundFriction_enabled) {
    float adaptedFriction = (pitch.quadratic_resistance() * grassInfluenceBias);

    Vector3 xy = momentumPredict.Get2D();
    float velo = xy.GetLength();

    float newVelo = velo - adaptedFriction * std::pow(velo, 2.0f) * timeStep;

    newVelo = clamp(newVelo - (pitch.ground_deceleration() * grassInfluenceBias * timeStep), 0.0f, 100000.0f);

    xy.Normalize(Vector3(0));
    xy *= newVelo;
    momentumPredict.coords[0] = xy.coords[0];
    momentumPredict.coords[1] = xy.coords[1];
  }

  // woodwork (posts + crossbar), only on the first prediction step.
  if (first_step && woodwork_enabled) {
    ResolveWoodwork(nextPos, momentumPredict, pitch, config);
  }

  // netting (side/rear/top), only on the first prediction step.
  if (first_step && netting_enabled) {
    ResolveNetting(nextPos, momentumPredict, pitch, config, environment);
  }

  // calculate rotation
  if (nextPos.coords[2] < config.radius() + pitch.grass_height() &&
      groundRotationEffects_enabled) {
    // ground friction induced rotation
    radian xR, yR;

    const float radius = config.radius();
    xR = momentumPredict.coords[1] / radius;
    yR = momentumPredict.coords[0] / radius;

    Quaternion rotX;
    rotX.SetAngleAxis(clamp(xR * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(-1, 0, 0));
    Quaternion rotY;
    rotY.SetAngleAxis(clamp(yR * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(0, 1, 0));

    Quaternion groundRot = rotX * rotY;

    Quaternion oldToNewRotation = rotationPredict_ms.GetRotationTo(groundRot).GetNormalized();
    radian rotationChangePerSecond = std::fabs(oldToNewRotation.GetRotationAngle(QUATERNION_IDENTITY)) * 1000.0f;

    radian maxRotationChangePerSecond = 1.0f * pi * grassInfluenceBias;
    if (frictionFactor > 0.0f) {
      maxRotationChangePerSecond += 4.0f * pi;
    }
    radian factor = 1.0f;
    if (rotationChangePerSecond > maxRotationChangePerSecond) {
      factor = maxRotationChangePerSecond / rotationChangePerSecond;
    }
    if (factor < 1.0f) {
      oldToNewRotation = oldToNewRotation.GetRotationMultipliedBy(factor);
    }

    Quaternion newRotationPredict_ms = oldToNewRotation * rotationPredict_ms;

    // rotation induced ground friction
    real x, y, z;
    rotationPredict_ms.GetAngles(x, y, z);
    x = -x;

    Vector3 ballRotationMomentum;
    ballRotationMomentum.coords[0] = y * radius * 1000.0f;
    ballRotationMomentum.coords[1] = x * radius * 1000.0f;

    float rotBias = 0.01f;
    rotBias *= grassInfluenceBias;
    if (frictionFactor > 0.0f) {
      rotBias += 0.5f * frictionFactor;
    }
    rotBias = clamp(rotBias, 0.0f, 1.0f);
    momentumPredict.coords[0] = momentumPredict.coords[0] * (1.0f - rotBias) + ballRotationMomentum.coords[0] * rotBias;
    momentumPredict.coords[1] = momentumPredict.coords[1] * (1.0f - rotBias) + ballRotationMomentum.coords[1] * rotBias;

    rotationPredict_ms = newRotationPredict_ms;
  }

  // magnus effect (swerve)
  if (swerve_enabled) {
    Vector3 rotVec;
    rotationPredict_ms.GetAngles(rotVec.coords[0], rotVec.coords[1], rotVec.coords[2]);
    rotVec *= 10.0f;

    float swerveAmount = NormalizedClamp(momentumPredict.GetLength(), 0.0f, 70.0f);
    swerveAmount = std::pow(std::sin(swerveAmount * pi * 0.94f), 2.6f);
    Vector3 adaptedMomentumPredict = momentumPredict.GetNormalized(0) * swerveAmount * 30.0f;

    Vector3 swerve = adaptedMomentumPredict.GetCrossProduct(-rotVec) * 1.0;

    momentumPredict += swerve * timeStep;
  }

  // predict next tick
  nextPos += momentumPredict * timeStep;

  Vector3 rotationVector;
  rotationPredict_ms.GetAngles(rotationVector.coords[0], rotationVector.coords[1], rotationVector.coords[2]);
  rotationVector *= timeStep / 0.001f;
  Quaternion rotationPredictTimeStepped;
  rotationPredictTimeStepped.SetAngles(rotationVector.coords[0], rotationVector.coords[1], rotationVector.coords[2]);

  nextOrientation = rotationPredictTimeStepped * nextOrientation;

  return PhysicsState{nextPos, momentumPredict, rotationPredict_ms, nextOrientation};
}

}  // namespace football::ball::detail