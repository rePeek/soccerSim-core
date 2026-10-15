// ANIM-1 A6.4 A8.0: the pure candidate reference-motion kernel. It lives in its
// own translation unit so the value-only unit test can exercise it without the
// locomotion runtime.
#include "sim/player/player_motion.hpp"

#include "football/animation/movement_math.hpp"
#include "sim/player/player_motion_constants.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

using football::animation::CalculateMovementAtFrame;
using football::animation::StretchSprintTo;

// ANIM-1 A6.4 A8.0: the selection-side reference-motion kernel, relocated
// line-for-line from `HumanoidBase::CalculatePhysicsVector`. Same operation
// order, same floating-point expressions and same boundary behaviour. It is a
// candidate plan, not a position commit: it never calls CommitKinematicState and
// never handles a Ball impulse.
ActionMotionPlan PlayerMotion::BuildActionMotionPlan(const AnimationClip& clip,
                                                     const ActionMotionInput& input) {
  ActionMotionPlan plan;

  const int animTouchFrame = clip.metadata.touch_frame;
  const bool touch = (animTouchFrame > 0);

  const float stat_agility = input.parameters.stat_agility;
  const float stat_acceleration = input.parameters.stat_acceleration;
  const float stat_velocity = input.parameters.stat_velocity;
  const float stat_dribble = input.parameters.stat_dribble;

  float incomingSwitchBias = 0.0f;  // anything other than 0.0 may result in unpuristic behavior
  float outgoingSwitchBias = 0.0f;

  const e_DefString animType = static_cast<e_DefString>(clip.metadata.action_type);

  if (animType == e_DefString_BallControl) {
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Trap) {
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Interfere) {
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Deflect) {
    outgoingSwitchBias = 1.0f;
  } else if (animType == e_DefString_Sliding) {
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Special) {
    outgoingSwitchBias = 1.0f;
  } else if (animType == e_DefString_Trip) {
    outgoingSwitchBias = 0.5f;  // direction partly predecided by collision function in match class
  } else if (touch) {
    outgoingSwitchBias = 1.0f;
  }

  if (!clip.metadata.incoming_special_state.empty() ||
      !clip.metadata.outgoing_special_state.empty())
    outgoingSwitchBias = 1.0f;

  Vector3 animIncomingMovement = Vector3(0, -1, 0).GetRotated2D(input.angle) * RangeVelocity(clip.metadata.incoming_velocity);
  Vector3 adaptedCurrentMovement = animIncomingMovement * incomingSwitchBias + input.spatial_movement * (1.0f - incomingSwitchBias);

  Vector3 predictedOutgoingMovement = clip.metadata.outgoing_movement.GetRotated2D(input.angle);
  Vector3 velocifiedDesiredMovement = (input.use_desired_movement) ? input.desired_movement : predictedOutgoingMovement;

  assert(input.desired_movement.coords[2] == 0.0f);

  Vector3 adaptedDesiredMovement = predictedOutgoingMovement * outgoingSwitchBias + velocifiedDesiredMovement * (1.0f - outgoingSwitchBias);
  assert(predictedOutgoingMovement.coords[2] == 0.0f);
  assert(velocifiedDesiredMovement.coords[2] == 0.0f);
  assert(adaptedDesiredMovement.coords[2] == 0.0f);

  float maxVelocity = input.parameters.max_velocity;
  if (touch) maxVelocity *= 0.92f;

  Vector3 resultingMovement;

  const int timeStep_ms = 10;

  const bool isBaseAnim = clip.metadata.base_animation;
  (void)isBaseAnim;

  const float difficultyFactor = clip.metadata.difficulty;
  float difficultyPenaltyFactor = std::pow(
      clamp((difficultyFactor - 0.0f) *
                (1.0f - (stat_agility * 0.2f + stat_acceleration * 0.2f)) *
                2.0f,
            0.0f, 1.0f),
      0.7f);

  float powerFactor = 1.0f - clamp(std::pow(input.parameters.last_touch_bias_1000, 0.8f) * (0.8f - stat_dribble * 0.3f), 0.0f, 0.4f);
  powerFactor *= 1.0f - clamp(input.parameters.decaying_position_offset_length * (10.0f - input.parameters.stat_balance * 5.0f) - 0.1f, 0.0f, 0.3f);

  Vector3 temporalMovement = adaptedCurrentMovement;

  assert(adaptedCurrentMovement.coords[2] == 0.0f);
  assert(adaptedDesiredMovement.coords[2] == 0.0f);

  // orig anim positions
  const std::vector<Vector3> &origPositionCache = clip.root_positions;

  Vector3 currentPosition;

  // amount of pure physics that 'shines through' pure anim
  float physicsBias = 1.0f;
  // angle deviation away from anim
  radian maxAngleMod_underAnimAngle = 0.125f * pi;
  radian maxAngleMod_overAnimAngle = 0.125f * pi;
  radian maxAngleMod_straightAnimAngle = 0.125f * pi;
  if (touch) {
    float bonus = 1.0f - std::pow(NormalizedClamp((adaptedCurrentMovement +
                                                   predictedOutgoingMovement)
                                                      .GetLength() *
                                                  0.5f,
                                                  0, sprintVelocity),
                                  0.8f) *
                             0.8f;
    bonus *= 0.6f + 0.4f * input.parameters.stat_technical_ballcontrol;
    maxAngleMod_underAnimAngle = 0.2f * pi * bonus;
    maxAngleMod_overAnimAngle = 0;
    maxAngleMod_straightAnimAngle = 0.1f * pi * bonus;
  }
  if (animType == e_DefString_Sliding) {
    maxAngleMod_underAnimAngle = 0.5f * pi;
    maxAngleMod_overAnimAngle = 0.5f * pi;
    maxAngleMod_straightAnimAngle = 0.5f * pi;
  }

  if (animType == e_DefString_Movement)    { physicsBias *= 1.0f; }

  if (animType == e_DefString_BallControl) {
    physicsBias *= 1.0f;
  }
  if (animType == e_DefString_Trap)        { physicsBias *= 1.0f; }

  if (animType == e_DefString_ShortPass)   { physicsBias *= 0.0f; }
  if (animType == e_DefString_HighPass)    { physicsBias *= 0.0f; }
  if (animType == e_DefString_Shot)        { physicsBias *= 0.0f; }

  if (animType == e_DefString_Interfere)   { physicsBias *= 0.5f; }
  if (animType == e_DefString_Deflect)     { physicsBias *= 0.0f; }

  if (animType == e_DefString_Sliding)     { physicsBias *= 1.0f; }
  if (animType == e_DefString_Trip)        { if (clip.metadata.trip_type == 1) physicsBias *= 0.5f; else physicsBias *= 0.0f; }

  if (animType == e_DefString_Special)     { physicsBias *= 0.0f; }
  if (!clip.metadata.incoming_special_state.empty())
                                           { physicsBias *= 0.0f; }

  bool mod_AllowRotation = true;
  bool mod_CorneringBraking = false;
  bool mod_PointinessCurve = true;
  bool mod_MaximumAccelDecel = false;
  bool mod_BrakeOnTouch = false;  // may be too pointy for anims near 90 degree
  bool mod_MaxCornering = true;
  bool mod_MaxChange = true;
  bool mod_AirResistance = true;
  bool mod_CheatBodyDirection = false;

  float accelerationMultiplier = 0.5f + _default_AccelerationFactor;

  // rotate anim towards desired angle

  radian toDesiredAngle_capped = 0;
  if (mod_AllowRotation && physicsBias > 0.0f) {
    Vector3 animOutgoingVector = predictedOutgoingMovement.GetNormalized(0);
    if (FloatToEnumVelocity(predictedOutgoingMovement.GetLength()) == e_Velocity_Idle) animOutgoingVector = clip.metadata.outgoing_direction.GetRotated2D(input.angle);
    Vector3 desiredVector = adaptedDesiredMovement.GetNormalized(0);
    if (FloatToEnumVelocity(adaptedDesiredMovement.GetLength()) == e_Velocity_Idle) desiredVector = input.desired_body_direction_rel.GetRotated2D(input.angle);
    radian toDesiredAngle = desiredVector.GetAngle2D(animOutgoingVector);
    if (std::fabs(toDesiredAngle) <= 0.5f * pi || animType == e_DefString_Sliding) {
      // if we want > x degrees, just skip it to next anim,
      // it'll only look weird otherwise

      radian animChange = animOutgoingVector.GetAngle2D(input.direction_vec);
      if (std::fabs(animChange) > 0.06f * pi) {
        int sign = signSide(animChange);
        if (signSide(toDesiredAngle) == sign) {
          toDesiredAngle_capped = clamp(toDesiredAngle, -maxAngleMod_overAnimAngle, maxAngleMod_overAnimAngle);
        } else {
          toDesiredAngle_capped = clamp(toDesiredAngle, -maxAngleMod_underAnimAngle, maxAngleMod_underAnimAngle);
        }
      } else {
        // straight ahead anim, has no specific side.
        toDesiredAngle_capped = clamp(toDesiredAngle, -maxAngleMod_straightAnimAngle, maxAngleMod_straightAnimAngle);
      }
      // printf("animChange: %f radians, to desired angle: %f radians, sign: %i, sign side of to desired angle: %i, resulting capped angle: %f radians\n", animChange, toDesiredAngle, sign, signSide(toDesiredAngle), toDesiredAngle_capped);
    }
  }

  float maximumOutgoingVelocity = sprintVelocity;
  // brake on cornering
  if (mod_CorneringBraking) {
    float brakeBias = 0.8f;
    brakeBias *= (touch) ? 1.0f : 0.8f;
    brakeBias *= (1.0f - stat_agility * 0.2f);

    Vector3 animOutgoingMovement = clip.metadata.outgoing_movement;
    animOutgoingMovement.Rotate2D(toDesiredAngle_capped);
    brakeBias *= std::pow(NormalizedClamp(input.float_velocity,
                                          idleVelocity, sprintVelocity - 0.5f),
                          0.8f);  // 0.5f);
    float maxVelo =
        sprintVelocity *
        ((1.0f - brakeBias) +
         ((1.0f -
           std::pow(std::fabs(animOutgoingMovement.GetNormalized(0).GetAngle2D(
                             Vector3(0, -1, 0)) /
                         pi),
                    0.5f)) *
          brakeBias));
    maximumOutgoingVelocity = maxVelo;
  }

  // --- loop da loop ------------------------------------------------------------------------------------------------------------------------------------------
  for (int time_ms = 0; time_ms < static_cast<int>(clip.frame_count) * 10;
       time_ms += timeStep_ms) {
    // start with +1, because we want to influence the first frame as well
    // as for finishing, finish with frameBias = 1.0, even if the last frame is 'spiritually' the one-to-last, since the first frame of the next anim is actually 'same-tempered' as the current anim's last frame.
    // however, it works best to have all values 'done' at this one-to-last frame, so the next anim can read out these correct (new starting) values.
    float frameBias = (time_ms + 10) / (float)(((static_cast<int>(clip.frame_count) - 1) + 1) * 10);

    float lagExp = 1.0f;
    if (mod_PointinessCurve && physicsBias > 0.0f &&
        (animType == e_DefString_BallControl ||
         animType == e_DefString_Movement)) {
      lagExp = 1.4f - _default_AgilityFactor * 0.8f;
      lagExp *= 1.2f - stat_agility * 0.4f;
      if (touch) {
        lagExp += -0.1f + clamp(difficultyFactor * 0.4f, 0.0f, 0.5f);
      } else {
        lagExp += -0.2f + clamp(difficultyFactor * 0.2f, 0.0f, 0.2f);
      }

      lagExp = clamp(lagExp, 0.25f, 4.0f);
      if (touch && time_ms < animTouchFrame * 10) lagExp = std::max(lagExp, 0.7f);  // else, we could 'miss' the ball because we're already turned around too much

      lagExp = lagExp * physicsBias + 1.0f * (1.0f - physicsBias);
    }
    float adaptedFrameBias = std::pow(frameBias, lagExp);
    Vector3 animMovement = CalculateMovementAtFrame(origPositionCache, (static_cast<int>(clip.frame_count) - 1) * adaptedFrameBias, 1).GetRotated2D(input.angle);

    float animVelo = animMovement.GetLength();
    Vector3 adaptedAnimMovement = animMovement;
    float adaptedAnimVelo = animVelo;

    // adapt sprint velocity to player's max velocity stat

    if (animVelo > walkSprintSwitch &&
        (animType == e_DefString_Movement ||
         animType == e_DefString_BallControl || animType == e_DefString_Trap)) {
      if (maxVelocity > animVelo) {
        // only speed up, don't slow down. may be faster parts
        // (jumps and such) within anim, allow this
        adaptedAnimVelo = StretchSprintTo(animVelo, animSprintVelocity, maxVelocity);
        adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;
      }
    }

    float maxSlower = 1.6f;  // rationale: don't want to end up below dribbleVelocity - idleDribbleSwitch (= change velocity)
    if (touch) maxSlower = 1.2f;
    float maxFaster = 0.0f;
    if (touch) maxFaster = 0.0f;
    if (temporalMovement.GetLength() > adaptedAnimVelo) maxFaster = std::min(0.0f + 1.0f * (1.0f - frameBias), std::max(maxFaster, temporalMovement.GetLength() - adaptedAnimVelo));  // ..already going faster.. well okay, allow this
    if (maxFaster > 0) maxFaster *= std::max(0.0f, adaptedAnimMovement.GetNormalizedMax(1.0f).GetDotProduct(adaptedDesiredMovement.GetNormalized(0)));  // only go faster if it's in the right direction
    if (animType == e_DefString_Sliding) maxFaster = 100;
    float desiredVelocity = adaptedDesiredMovement.GetLength();
    adaptedAnimVelo = clamp(desiredVelocity, adaptedAnimVelo - maxSlower, adaptedAnimVelo + maxFaster);
    adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;

    if (mod_CorneringBraking) {
      float frameBiasedMaximumOutgoingVelocity = sprintVelocity * (1.0f - frameBias) + maximumOutgoingVelocity * frameBias;
      if (adaptedAnimVelo > frameBiasedMaximumOutgoingVelocity) {
        adaptedAnimVelo = frameBiasedMaximumOutgoingVelocity;
        adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;
      }
    }

    if (mod_MaximumAccelDecel) {
      // this is basically meant to enforce transitions to be smoother, disallowing bizarre steps. however, with low enough max values, it can also serve as a physics slowness thing.
      // in that regard, the maxaccel part is somewhat similar to the air resistance mod below. they can live together; this one can serve as a constant maximum, and the air resistance as a velocity-based maximum.
      // update: decided to not let them live together, this should now purely be used for capping transition speed
      float maxAccelMPS = 20.0f;
      float maxDecelMPS = 20.0f;
      float currentVelo = temporalMovement.GetLength();
      float veloChangeMPS = (adaptedAnimVelo - currentVelo) / ((float)timeStep_ms * 0.001f);
      if (veloChangeMPS < -maxDecelMPS || veloChangeMPS > maxAccelMPS) {
        adaptedAnimVelo = currentVelo + clamp(veloChangeMPS, -maxDecelMPS, maxAccelMPS) * ((float)timeStep_ms * 0.001f);
        adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;
      }
    }

    Vector3 resultingPhysicsMovement = adaptedAnimMovement;

    // angle
    resultingPhysicsMovement = resultingPhysicsMovement.GetRotated2D(toDesiredAngle_capped * frameBias);  // * std::pow(frameBias, 0.6f));

    // --- stay true to anim? -----------------------------------------------------------------------------------------------------------------------

    resultingPhysicsMovement = resultingPhysicsMovement * physicsBias + animMovement * (1.0f - physicsBias);

    // that's it, we now know where we want to go in life

    Vector3 toDesired = (resultingPhysicsMovement - temporalMovement);

    // --- end --------------------------------------------------------------------------------------------------------------------------------------

    assert(toDesired.coords[2] == 0.0f);

    float penaltyBreakFactor = 0.0f;
    if (mod_BrakeOnTouch) {
      // slow down after touching ball
      // (precalc at touchframe, because temporalMovement will change because of this, so if we don't precalc then changing numBrakeFrames will change the amount of effect)
      int numBrakeFrames = 15;
      if (touch && time_ms >= animTouchFrame * 10 &&
          time_ms < (animTouchFrame + numBrakeFrames) * 10) {
        int brakeFramesInto = (time_ms - (animTouchFrame * 10)) / 10;
        float brakeFrameFactor =
            std::pow(1.0f - (brakeFramesInto / (float)numBrakeFrames), 0.5f);

        float touchBrakeFactor = 0.3f;

        float touchDifficultyFactor = clamp((difficultyFactor + 0.7f) * (1.0f - stat_dribble * 0.4f), 0.0, 1.0f);
        touchDifficultyFactor *=
            1.0f -
            std::pow(
                std::fabs(clip.metadata.outgoing_angle) / pi,
                0.75f);  // don't help with braking (when going nearer 180 deg)

        float veloFactor = NormalizedClamp(temporalMovement.GetLength(), walkVelocity, sprintVelocity);

        penaltyBreakFactor = touchDifficultyFactor * veloFactor * brakeFrameFactor * touchBrakeFactor;
      }
    }

    /* part of new method, but needs some debugging/unittesting
    // increase over multiple frames for smoother effect, and so we get a proper
    effect even if maxChange isn't very high int numBrakeFrames = 5; if (touch
    && time_ms >= animTouchFrame * 10 && time_ms < (animTouchFrame +
    numBrakeFrames) * 10) { int framesInto = (time_ms -
    (animTouchFrame * 10)) / 10; toDesired += -temporalMovement.GetNormalized(0)
    * (ballTouchSlowdownAmount / (float)(numBrakeFrames - framesInto));
    }
    */

    if (mod_MaxCornering) {
      Vector3 predictedMovement = temporalMovement + toDesired;
      float startVelo = idleDribbleSwitch;
      if (temporalMovement.GetLength() > startVelo &&
          predictedMovement.GetLength() > startVelo) {
        radian angle = predictedMovement.GetNormalized().GetAngle2D(temporalMovement.GetNormalized());
        float maxAngleFactor = 1.0f * (timeStep_ms / 1000.0f);
        maxAngleFactor *= (0.7f + 0.3f * stat_agility);
        if (!touch) maxAngleFactor *= 1.5f;
        radian maxAngle = maxAngleFactor * pi;
        float veloFactor = std::pow(
            NormalizedClamp(temporalMovement.GetLength(), 0, sprintVelocity),
            1.0f);
        maxAngle /= (veloFactor + 0.01f);

        if (std::fabs(angle) > maxAngle) {
          int mode = 1;  // 0: restrict max angle, 1: restrict velocity

          if (mode == 0) {
            Vector3 restrictedPredictedMovement = predictedMovement.GetRotated2D((std::fabs(angle) - maxAngle) * -signSide(angle));
            Vector3 newToDesired = restrictedPredictedMovement - temporalMovement;
            toDesired = newToDesired;
          } else if (mode == 1) {
            radian overAngle = std::fabs(angle) - maxAngle;  // > 0
            toDesired += -temporalMovement * clamp(overAngle / pi * 3.0f, 0.0f, 1.0f);  // was: 3
          }
        }
      }
    }

    if (mod_MaxChange) {
      float maxChange = 0.03f;
      if (animType == e_DefString_Trip) maxChange *= 0.7f;
      if (animType == e_DefString_Sliding) maxChange = 0.1f;
      // no power first few frames, so transitions are smoother
      // maxChange *= 0.3f + 0.7f * curve(NormalizedClamp(time_ms, 0.0f, 80.0f), 1.0f);
      float veloFactor = std::pow(
          NormalizedClamp(temporalMovement.GetLength(), 0, sprintVelocity),
          1.5f);
      float firstStepFactor = veloFactor;
      if (animType == e_DefString_Movement) firstStepFactor *= 0.4f;
      maxChange *= (1.0f - firstStepFactor) + firstStepFactor * curve(NormalizedClamp(time_ms, 0.0f, 160.0f), 1.0f);

      maxChange *= 1.2f - veloFactor * 0.4f;

      maxChange *= 0.75f + _default_AgilityFactor * 0.5f;

      // lose power 'around' touch

      // if (touch && time_ms >= animTouchFrame * 10) {
      //   int influenceFrames = 16; // number of frames to slow down on each
      //   'side' of the balltouch
      //   //float frameBias = NormalizedClamp(std::fabs((animTouchFrame * 10) -
      //   time_ms), 0, influenceFrames * 10) * 0.7f + 0.3f; float frameBias =
      //   NormalizedClamp(time_ms - (animTouchFrame * 10), 0, influenceFrames *
      //   10) * 1.0f;// + 0.1f; frameBias = curve(frameBias, 1.0f); maxChange
      //   *= clamp(frameBias, 0.1f, 1.0f);
      // }

      maxChange *= powerFactor;

      float desiredLength = toDesired.GetLength();
      float maxAddition = maxChange * timeStep_ms;

      toDesired.NormalizeMax(std::min(desiredLength, maxAddition));
    }

    // air resistance

    if (mod_AirResistance && animType != e_DefString_Sliding &&
        animType != e_DefString_Deflect) {
      float veloExp = 1.8f;
      float accelPower = 11.0f * accelerationMultiplier;
      float falloffStartVelo = idleDribbleSwitch;

      if ((temporalMovement + toDesired).GetLength() > falloffStartVelo) {
        // less accelpower on tough anims
        accelPower *= 1.0f - difficultyPenaltyFactor * 0.4f;

        // wolfram alpha to show difference in sin before/after exp order: 10 * (std::sin(((1.0 - x ^ 2.0) - 0.5) * pi) * 0.5 + 0.5), 10 * ((1.0 - (std::sin((x - 0.5) * pi) * 0.5 + 0.5)) ^ 2.0) | from x = 0.0 to 1.0

        float veloAirResistanceFactor = clamp(
            std::pow(clamp((temporalMovement.GetLength() - falloffStartVelo) /
                               (input.parameters.max_velocity - falloffStartVelo),
                           0.0f, 1.0f),
                     veloExp),
            0.0f, 1.0f);

        // simulate footsteps - powered in the middle
        // veloAirResistanceFactor = 1.0f - ((1.0f - veloAirResistanceFactor) * std::pow(sin(frameBias * pi), 2.0f));

        // circular version
        Vector3 forwardVector;
        if ((temporalMovement + toDesired).GetLength() >
            temporalMovement.GetLength()) {
          // outside the 'velocity circle'
          Vector3 destination = temporalMovement + toDesired;
          float velo = temporalMovement.GetLength();
          float accel = destination.GetLength() - velo;
          forwardVector = destination.GetNormalized(0) * accel;
        }

        float accelerationAddition = forwardVector.GetLength();
        float maxAccelerationMPS = accelPower * (1.0f - veloAirResistanceFactor) * (stat_acceleration * 0.3f + 0.7f);
        float maxAccelerationAddition = maxAccelerationMPS * (timeStep_ms / 1000.0f);
        if (accelerationAddition > maxAccelerationAddition) {
          float remainingFactor = maxAccelerationAddition / accelerationAddition;
          toDesired = toDesired - forwardVector * (1.0f - remainingFactor);
        }
      }
    }

    // MAKE IT SEW! http://static.wixstatic.com/media/fc58ad_c0ef2d69d98f4f7ba8e7e488f0e28ece.jpg

    Vector3 tmpTemporalMovement = temporalMovement + toDesired;

    // make sure outgoing velocity is of the same idleness as the anim
    if (time_ms >= (static_cast<int>(clip.frame_count) - 2) * 10) {
      bool hardQuantize = true;
      if (!hardQuantize && !clip.metadata.outgoing_special_state.empty()) hardQuantize = true;

      if (!hardQuantize) {
        // soft version
        if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) == e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) != e_Velocity_Idle) tmpTemporalMovement.NormalizeTo(idleDribbleSwitch - 0.01f);
        else if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) != e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) == e_Velocity_Idle) tmpTemporalMovement = clip.metadata.outgoing_movement.GetRotated2D(input.angle).GetNormalizedTo(idleDribbleSwitch + 0.01f);
      } else {
        // hard version
        if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) == e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) != e_Velocity_Idle) tmpTemporalMovement = 0;
        else if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) != e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) == e_Velocity_Idle) tmpTemporalMovement = clip.metadata.outgoing_movement.GetRotated2D(input.angle).GetNormalizedTo(dribbleVelocity);
      }
    }

    assert(tmpTemporalMovement.coords[2] == 0.0f);
    temporalMovement = tmpTemporalMovement;

    if (time_ms >= (static_cast<int>(clip.frame_count) - 2) * 10) penaltyBreakFactor = 0.0f;
    currentPosition += temporalMovement * (1.0f - penaltyBreakFactor) * (timeStep_ms / 1000.0f);
    assert(currentPosition.coords[2] == 0.0f);

    if (time_ms % 10 == 0) {
      plan.positions.push_back(currentPosition);
    }

    /*
    // dynamic timestep: more precision at high velocities
    if ((int)time_ms % 10 == 0) {
      //if (temporalMovement.GetLength() > walkVelocity) timeStep_ms = 5; else
    timeStep_ms = 10; timeStep_ms = 10;
    }
    */
  }

  assert(plan.positions.size() >= (unsigned int)static_cast<int>(clip.frame_count));
  resultingMovement = temporalMovement;

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) != e_Velocity_Idle &&
      FloatToEnumVelocity(resultingMovement.GetLength()) != e_Velocity_Idle) {
    plan.rotation_offset = resultingMovement.GetRotated2D(-input.angle).GetAngle2D(clip.metadata.outgoing_movement);
  } else {
    plan.rotation_offset = toDesiredAngle_capped * physicsBias;
  }

  // body direction assist
  if (mod_CheatBodyDirection && input.use_desired_body_direction &&
      animType == e_DefString_Movement) {
    float angleFactor = 0.5f;
    radian maxAngle = 0.25f * pi;

    radian predictedAngleRel = clip.metadata.outgoing_angle + clip.metadata.outgoing_body_angle + plan.rotation_offset;
    radian desiredRotationOffset = input.desired_body_direction_rel.GetRotated2D(-predictedAngleRel).GetAngle2D(Vector3(0, -1, 0));

    if (std::fabs(desiredRotationOffset) < 0.5f * pi) {
      // else: too much

      float outgoingVelocityFactorInv = 1.0f - NormalizedClamp(resultingMovement.GetLength(), idleDribbleSwitch, sprintVelocity - 1.0f) * 1.0f;
      float animLengthFactor = NormalizedClamp(static_cast<int>(clip.frame_count), 0, 25);
      radian maximizedRotationOffset = clamp(desiredRotationOffset, outgoingVelocityFactorInv * animLengthFactor * angleFactor * -maxAngle,
                                                                    outgoingVelocityFactorInv * animLengthFactor * angleFactor *  maxAngle);

      plan.rotation_offset += maximizedRotationOffset;
    }
  }

  assert(resultingMovement.coords[2] == 0.0f);

  plan.outgoing_velocity = resultingMovement;
  return plan;
}
