#ifndef FOOTBALL_ANIMATION_TOUCH_SCORING_HPP
#define FOOTBALL_ANIMATION_TOUCH_SCORING_HPP

#include <algorithm>
#include <cmath>

#include "football/animation/types.hpp"
#include "football/animation/clip.hpp"

// ANIM-1 A6.4 step 2a - stateless touch/action-smuggle scoring helpers.
//
// These are line-for-line relocations of pure arithmetic inside
// `Humanoid::GetBestCheatableAnimID`. They read no Player / Humanoid /
// Simulation state: everything they need travels in the request struct, so the
// Animation side owns the formula and the Player side owns the inputs.
//
// Behaviour is intended to be bit-identical: same clamping, same operation
// order, same floating-point expressions.

namespace football::animation {

// The tail of `GetBestCheatableAnimID` that turns the raw ball-contact smuggle
// vector into the value actually applied to the player. `raw_smuggle` is the
// world-space (already rotated) value the caller computed as
// `best_action_smuggle_vec2d.GetRotated2D(spatial_angle)`.
struct ResolveActionSmuggleRequest {
  Vector3 raw_smuggle{0};
  float touch_pos_z = 0.0f;
  int anim_touch_frame = 0;
  e_FunctionType function_type = e_FunctionType_None;
  bool ball_retained_by_player = false;
  radian predicted_angle = 0.0f;
  radian spatial_angle = 0.0f;
  Vector3 adapted_outgoing_movement{0};

  bool force_full_action_smuggle_discard = false;
  bool enable_action_smuggle_discard = false;
  float cheat_discard_distance = 0.0f;
  float cheat_discard_distance_multiplier = 0.0f;
  float max_smuggle_discard_distance = 0.0f;
  bool discard_forward_smuggle = false;
  bool discard_sideways_smuggle = false;

  float closest_opponent_distance = 1.0f;
};

inline Vector3 ResolveActionSmuggle(const ResolveActionSmuggleRequest& request) {
  Vector3 action_smuggle = request.raw_smuggle;

  if (request.force_full_action_smuggle_discard) {
    action_smuggle = 0;
  } else if (request.enable_action_smuggle_discard) {
    // cheat discard distance (don't show some amount of cheat)
    float smuggleDistance = action_smuggle.GetLength();
    float adaptedCheatDiscardDistanceMultiplier = request.cheat_discard_distance_multiplier;
    if (request.function_type != e_FunctionType_BallControl) adaptedCheatDiscardDistanceMultiplier *= 0.8f;
    if (request.touch_pos_z > 0.5f) adaptedCheatDiscardDistanceMultiplier *= 0.7f;
    if (request.touch_pos_z > 1.0f) adaptedCheatDiscardDistanceMultiplier *= 0.7f;
    float cheatDiscardDistanceBonus = 0.0f;
    if (request.function_type == e_FunctionType_Interfere) cheatDiscardDistanceBonus += 0.1f;  // don't break flow

    // smuggle, in this context, is how much we will move to the ball after the
    // physics have been accounted for.
    smuggleDistance = clamp(smuggleDistance - (request.cheat_discard_distance + cheatDiscardDistanceBonus), 0.0f, 100.0);
    smuggleDistance *= 1.0f - adaptedCheatDiscardDistanceMultiplier;
    smuggleDistance = std::min(smuggleDistance, action_smuggle.GetLength() - request.max_smuggle_discard_distance);

    if (request.ball_retained_by_player) smuggleDistance = 0.0f;
    action_smuggle = action_smuggle.GetNormalized(0) * smuggleDistance;

    // lose forward-facing part of smuggle
    if (request.discard_forward_smuggle || request.discard_sideways_smuggle) {
      radian toStraightAngle = request.spatial_angle + request.predicted_angle;
      action_smuggle.Rotate2D(-toStraightAngle);

      if (request.discard_forward_smuggle) {
        float shortenForwardDistance = 0.02f;
        float allowForwardDistance =
            0.25f *
            (1.0f -
             std::pow(NormalizedClamp(request.adapted_outgoing_movement.GetLength(),
                                      idleVelocity, sprintVelocity - 2.0f),
                      0.6f)) *
            (request.anim_touch_frame * 0.1f);
        if (request.function_type != e_FunctionType_BallControl && request.function_type != e_FunctionType_Trap) allowForwardDistance += 0.1f;
        if (action_smuggle.coords[1] < 0.0f) action_smuggle.coords[1] = clamp(action_smuggle.coords[1] + shortenForwardDistance, -allowForwardDistance, 0.0f);
      }
      if (request.discard_sideways_smuggle) {
        action_smuggle.coords[0] *= 0.7f;
      }

      action_smuggle.Rotate2D(toStraightAngle);
    }

    // less chaos in micro battles
    action_smuggle *= 0.7f + 0.3f * NormalizedClamp(request.closest_opponent_distance, 0.6f, 1.2f);
  }

  return action_smuggle;
}


// ANIM-1 A6.4 step 9c: the pure body/ball distance-advantage scoring relocated
// from `Humanoid::GetBodyBallDistanceAdvantage`. Only `frame_count` is needed
// from the clip; every other fact travels in the arguments. The two cheat tuning
// values are passed explicitly so Animation owns no gameplay tuning.
inline float EvaluateBodyBallDistanceAdvantage(
    int frame_count, const Vector3 &animTouchMovement,
    const Vector3 &touchMovement, const Vector3 &incomingMovement,
    const Vector3 &outgoingMovement, radian outgoingAngle,
    const Vector3 &bodyPos, const Vector3 &FFO, const Vector3 &animBallPos2D,
    const Vector3 &actualBallPos2D, float radiusFactor,
    float radiusCheatDistance, float cheat_factor,
    float cheat_distance_bonus) {
  assert(touchMovement.coords[2] == 0.0f);
  assert(bodyPos.coords[2] == 0.0f);

  float touchVelocity = touchMovement.GetLength();
  float animTouchVelocity = animTouchMovement.GetLength();
  float highestTouchVelocity = (animTouchVelocity > touchVelocity) ? animTouchVelocity : touchVelocity;

  float incomingVelocity = incomingMovement.GetLength();
  float outgoingVelocity = outgoingMovement.GetLength();
  float averageInOutVelocity = (incomingMovement + outgoingMovement).GetLength() * 0.5f;
  float highestInOutVelocity = (incomingVelocity > outgoingVelocity) ? incomingVelocity : outgoingVelocity;

  float highestVelocity = (highestTouchVelocity > highestInOutVelocity) ? highestTouchVelocity : highestInOutVelocity;
  (void)highestVelocity;

  float velocityChange = outgoingVelocity - incomingVelocity;
  float velocityChange_mps = velocityChange / (static_cast<int>(frame_count) * 0.01f);

  float bodyAnimBallBonus = 1.0f - curve(NormalizedClamp(((bodyPos + FFO.GetNormalized(0) * 0.1f) - animBallPos2D).GetLength(), 0.0f, 0.7f), 0.7f); // less FFO feels better
  float bodyActualBallBonus = 1.0f - curve(NormalizedClamp(((bodyPos + FFO.GetNormalized(0) * 0.1f) - actualBallPos2D).GetLength(), 0.0f, 0.7f), 0.4f);
  float velocityBonus = 1.0f - NormalizedClamp(averageInOutVelocity, idleVelocity, sprintVelocity);
  float velocityChangeBonus = 1.0f - NormalizedClamp(velocityChange_mps / 20.0f, -1.0f, 1.0f);

  float radius = radiusFactor;
  radius *= 1.0f +
            1.0f * bodyAnimBallBonus +
            0.6f * bodyActualBallBonus +
            0.8f * bodyAnimBallBonus * bodyActualBallBonus +
            0.0f * velocityChangeBonus +
            1.0f * velocityBonus;

  float effectiveRadiusCheatDistance = radiusCheatDistance;

  Vector3 outgoingDirection;
  if (FloatToEnumVelocity(outgoingMovement.GetLength()) == e_Velocity_Idle) {
    outgoingDirection = Vector3(0, -1, 0).GetRotated2D(outgoingAngle);
  } else {
    outgoingDirection = outgoingMovement.GetNormalized();
  }

  Vector3 behindVectorUnscaled = -(incomingMovement * 0.1f + touchMovement * 0.2f + outgoingMovement * 0.7f);
  Vector3 behindVector =
      behindVectorUnscaled.GetNormalized(0) *
      std::pow(
          NormalizedClamp(behindVectorUnscaled.GetLength(), 0, sprintVelocity),
          0.5f);

  // cornering with a more centered behindvector for 'richer' range
  float dot = Vector3(0, -1, 0).GetDotProduct(outgoingDirection);
  dot = 0.5f + clamp(dot * 0.5f + 0.5f, 0.0f, 1.0f) * 0.5f;
  behindVector *= dot;

  Vector3 animToActualBall = actualBallPos2D - animBallPos2D;

  bool deformArea = true;
  if (deformArea) {
    Vector3 straightAngleVectorUnscaled = incomingMovement;
    Vector3 straightAngleVector = straightAngleVectorUnscaled.GetNormalized(outgoingDirection);
    radian toStraightAngle = Vector3(0, -1, 0).GetAngle2D(straightAngleVector);

    // rotate animball to actualball vec into outgoing direction so we can do funky stuff with it
    animToActualBall.Rotate2D(toStraightAngle);

    // cheating to the side (lateral) (as seen from the outgoing direction vector) is harder at high velos (effectively changes cheat circle into ellipse)
    float lateralRadiusFactor =
        0.6f -
        0.3f * std::pow(NormalizedClamp(straightAngleVectorUnscaled.GetLength(),
                                        idleVelocity, sprintVelocity),
                        0.7f);
    animToActualBall.coords[0] /= lateralRadiusFactor; // coords[0] is now the lateral component
    radius *= std::pow(
        1.0f / lateralRadiusFactor,
        0.5f);  // make sure we stick with the same surface area (= pi * r ^ 2,
                // so just take the sqrt of lateralradiusfactor, which is
                // effectively a surface area multiplier)

    // rotate back and act like nothing happened
    animToActualBall.Rotate2D(-toStraightAngle);
  }

  // do some magic

  Vector3 adaptedActualBallPos2D = animBallPos2D + animToActualBall;

  float radiusCheatBehindBias = 0.6f; // how much the radiuscheat heeds behindvec
  Vector3 behindCenter = animBallPos2D + behindVector * (radius + (effectiveRadiusCheatDistance * radiusCheatBehindBias)) * cheat_factor;

  float result = 1.0f;
  float allowedRadius = (radius + effectiveRadiusCheatDistance) * cheat_factor + cheat_distance_bonus;
  if (adaptedActualBallPos2D.GetDistance(behindCenter) > allowedRadius) {
    result = 0.0f;
  }

  return result;
}

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_TOUCH_SCORING_HPP
