// ANIM-1 A6.4 step 9a/10: the action-selection flow is Player-owned. This
// translation unit holds the relocated `SelectAnim` body, the crude-query
// builder and the `NeedTouch` gate. The Humanoid is only a compatibility
// provider of the not-yet-migrated spatial facts; it no longer decides.
//
// The pure scoring/refinement algorithms stay in Animation; the reference
// motion comes from the A8.0 PlayerMotion kernel. Nothing about candidate
// order, filter order, RNG timing or install timing changed.

#include "sim/player/player.hpp"

#include "football/animation/library.hpp"
#include "football/animation/movement_math.hpp"
#include "football/animation/selector.hpp"
#include "football/ball/ball.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/humanoid/humanoid.hpp"
#include "sim/player/locomotion_animation_strategy.hpp"
#include "sim/player/player_motion_constants.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <vector>

using namespace football::math;
using std::placeholders::_1;
using std::placeholders::_2;

// ---------------------------------------------------------------------------
// ANIM-1 A6.4 step 9a: the crude candidate constraints, built by the Player.
// `CrudeSelectionQuery query;` is the first statement of the original block.
// ---------------------------------------------------------------------------
CrudeSelectionQuery Player::BuildCrudeSelectionQuery(
    const PlayerCommand &command, const football::sim::PlayerTickContext &tick,
    football::sim::Tick now, MentalImage *current_mental_image) {
  Humanoid *const humanoid = CastHumanoid();
  const SpatialState &spatialState = humanoid->spatialState;
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = command.desiredFunctionType;

  query.byFoot = false;
  query.foot = spatialState.foot == e_Foot_Left ? e_Foot_Right : e_Foot_Left;

  // query.heedForcedFoot = true;
  // query.strongFoot = e_Foot_Right;

  // hax: long pass uses same anims as short pass
  if (query.functionType == e_FunctionType_LongPass) query.functionType = e_FunctionType_ShortPass;

  if (command.touchInfo.desiredPower != 0.0f) {
    query.byOutgoingBallDirection = true;
    query.outgoingBallDirection = command.touchInfo.desiredDirection.GetRotated2D(-spatialState.angle);
  }

  query.byIncomingVelocity = true;
  query.incomingVelocity = spatialState.enumVelocity;

  if (query.functionType != e_FunctionType_Movement && query.incomingVelocity == e_Velocity_Dribble) query.incomingVelocity = e_Velocity_Walk;

  query.incomingVelocity_Strict = false;
  if (query.functionType != e_FunctionType_Movement &&
      query.functionType != e_FunctionType_BallControl) {
    query.incomingVelocity_ForceLinearity = false;
    if (query.functionType != e_FunctionType_Deflect) {
      query.incomingVelocity_NoDribbleToSprint = true;
      if (query.functionType != e_FunctionType_ShortPass &&
          query.functionType != e_FunctionType_LongPass &&
          query.functionType != e_FunctionType_HighPass &&
          query.functionType != e_FunctionType_Shot) {
        query.incomingVelocity_ForceLinearity = true;
        query.incomingVelocity_NoDribbleToIdle = true;
      } else {  // passes and such
        query.incomingVelocity_ForceLinearity = false;
        query.incomingVelocity_NoDribbleToIdle = false;
      }
    } else {  // deflect
      query.incomingVelocity_NoDribbleToSprint = false;
      query.incomingVelocity_NoDribbleToIdle = false;
    }
  } else {
    query.incomingVelocity_Strict = true;
  }

  query.byIncomingBodyDirection = true;

  query.incomingBodyDirection = spatialState.relBodyDirectionVec;
  if (query.functionType != e_FunctionType_Movement) {
    query.incomingBodyDirection_Strict = false;
    if (query.functionType != e_FunctionType_Deflect) {
      if (query.functionType != e_FunctionType_BallControl) {
        query.incomingBodyDirection_ForceLinearity = true;
      } else {
        query.incomingBodyDirection_ForceLinearity = false;  // new, we want to be able to use ballcontrol anims as trap more often to stop ball from rolling past us
      }
    } else {  // deflect
      query.incomingBodyDirection_ForceLinearity = false;
    }
  } else {
    query.incomingBodyDirection_Strict = true;
  }

  query.bySide = false;
  if (command.useDesiredLookAt &&
      humanoid->GetCurrentBakedClip().metadata.outgoing_special_state.empty() &&
      tick.ball_retainer != this) {
    Vector3 playerLookAtVec = (command.desiredLookAt - spatialState.position).GetNormalized(spatialState.directionVec);
    query.lookAtVecRel = playerLookAtVec.GetRotated2D(-spatialState.angle);
    query.bySide = true;
  }

  if (command.onlyDeflectAnimsThatPickupBall == true) {
    query.byPickupBall = true;
    query.pickupBall = true;
  }

  if (command.desiredFunctionType == e_FunctionType_Trap ||
      command.desiredFunctionType == e_FunctionType_Interfere ||
      command.desiredFunctionType == e_FunctionType_Deflect) {
    query.byIncomingBallDirection = true;
    query.incomingBallDirection = (current_mental_image->GetBallPrediction(180, now, tick.ball) - current_mental_image->GetBallPrediction(120, now, tick.ball)).GetRotated2D(-spatialState.angle).GetNormalized(Vector3(0));
  }

  if (AllowLastDitch()) {
    query.allowLastDitchAnims = true;
  } else {
    query.allowLastDitchAnims = false;
  }

  if (command.desiredFunctionType == e_FunctionType_Trip) {
    query.byTripType = true;
    query.tripType = command.tripType;
  }

  query.properties.incoming_special_state = humanoid->GetCurrentBakedClip().metadata.outgoing_special_state;
  if (tick.ball_retainer == this) query.properties.incoming_retain_state = humanoid->GetCurrentBakedClip().metadata.outgoing_retain_state;
  if (command.useSpecialVar1) query.properties.special_var1 = command.specialVar1;
  if (command.useSpecialVar2) query.properties.special_var2 = command.specialVar2;

  if (!humanoid->GetCurrentBakedClip().metadata.outgoing_special_state.empty()) query.incomingVelocity = e_Velocity_Idle;  // standing up anims always start out idle
  return query;
}

// ---------------------------------------------------------------------------
// ANIM-1 A6.4 step 10: the touch gate, relocated from Humanoid::NeedTouch.
// ---------------------------------------------------------------------------
bool Player::NeedTouch(football::sim::Tick now,
                       const football::sim::PlayerTickContext &tick, int animID,
                       const PlayerCommand &command,
                       std::span<MentalImage> history) {
  Humanoid *const humanoid = CastHumanoid();
  const SpatialState &spatialState = humanoid->spatialState;

  // when idle (and desiredvelo is idle as well), don't want to touch the ball every frame

  const AnimationClip &clip = humanoid->GetBakedClip(animID);

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity != e_Velocity_Idle)) return true;
  if (command.desiredVelocityFloat > idleDribbleSwitch) return true;
  if (std::fabs(tick.ball.state().velocity.GetLength()) > 2.0f) return true;

  Vector3 animMovement = clip.metadata.outgoing_movement.GetRotated2D(spatialState.angle) * 0.3f + spatialState.movement * 0.7f;

  float animVelo = animMovement.GetLength();
  animMovement.Normalize(spatialState.directionVec);
  animMovement *= spatialState.movement.GetLength() * 0.8f + animVelo * 0.2f;
  auto currentMentalImage =
      football::sim::observation::SampleMentalImage(history, humanoid->mentalImageTime);
  Vector3 ballMovement = (currentMentalImage->GetBallPrediction(250, now, tick.ball).Get2D() - currentMentalImage->GetBallPrediction(240, now, tick.ball).Get2D()) * 100;

  if (std::fabs(clip.metadata.outgoing_angle) > 0.125f * pi) return true;

  float distanceDeviation = (animMovement - ballMovement).GetLength();
  if (distanceDeviation >= 2.0) return true;

  float velocityDeviation = animMovement.GetLength() - ballMovement.GetLength();  // negative == ball is faster
  if (velocityDeviation < -1.4 || velocityDeviation >= 0.7) return true;

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) != e_Velocity_Idle) {
    float angleDeviation = animMovement.GetNormalized(spatialState.directionVec).GetDotProduct(ballMovement.GetNormalized(spatialState.directionVec));
    if (angleDeviation < 0.975) return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// ANIM-1 A6.4 step 10: the selection flow. Relocated verbatim from
// Humanoid::SelectAnim; the Humanoid only supplies spatial facts.
// ---------------------------------------------------------------------------
bool Player::TrySelectAction(football::sim::Tick now,
                             const football::sim::PlayerTickContext &tick,
                             const PlayerCommand &command,
                             std::span<MentalImage> history,
                             e_InterruptAnim localInterruptAnim,
                             bool preferPassAndShot,
                             bool allowPreTouchRotationSmuggle,
                             bool useContinuousBallCheck) {
  Humanoid *const humanoid = CastHumanoid();
  if (humanoid == nullptr) return false;
  const SpatialState &spatialState = humanoid->spatialState;
  Anim &currentAnim = humanoid->currentAnim;

  // returns false on no applicable anim found
  assert(command.desiredDirection.coords[2] == 0.0f);
  const PlayerActionState &action = actionState;
  const bool material_candidate =
      RecordSchedulerQuery(currentAnim.originatingCommand, command);

  // optimizations
  auto currentMentalImage = football::sim::observation::SampleMentalImage(
      history, humanoid->mentalImageTime);
  if (command.desiredFunctionType != e_FunctionType_Movement &&
      command.desiredFunctionType != e_FunctionType_Trip &&
      command.desiredFunctionType != e_FunctionType_Special &&
      command.desiredFunctionType != e_FunctionType_Sliding) {
    if ((currentMentalImage->GetBallPrediction(200, now, tick.ball).Get2D() -
         spatialState.position)
            .GetLength() > 10.0f) {
      return false;
    }
    if ((currentMentalImage->GetBallPrediction(defaultTouchOffset_ms, now, tick.ball).Get2D() -
         spatialState.position)
                .GetLength() > 2.0f &&
        (currentMentalImage->GetBallPrediction(defaultTouchOffset_ms, now, tick.ball).Get2D() -
         (spatialState.position +
          spatialState.movement * defaultTouchOffset_ms * 0.001))
                .GetLength() >
            (currentMentalImage->GetBallPrediction(0, now, tick.ball).Get2D() -
             (spatialState.position))
                .GetLength()) {
      // ball moving away from player
      return false;
    }
  }

  // this stops these anims from happening when opp has touched the ball, deflecting the ball too far away
  if (command.desiredFunctionType != e_FunctionType_Movement &&
      command.desiredFunctionType != e_FunctionType_Trip &&
      command.desiredFunctionType != e_FunctionType_Special &&
      command.desiredFunctionType != e_FunctionType_Sliding &&
      command.desiredFunctionType != e_FunctionType_Deflect &&
      tick.ball_retainer != this) {
    if ((currentMentalImage->GetBallPrediction(1000, now, tick.ball) - tick.ball.Predict(football::sim::TickSpan{100}).position).GetLength() > 2.0f) return false;
  }

  // /optimizations

  if (localInterruptAnim == e_InterruptAnim_ReQueue) {
    // ANIM-1 A6.4 step 6a: the pre-search ReQueue gate is a Player-owned pure
    // decision. The Humanoid supplies the not-yet-migrated legacy facts.
    football::sim::RequeueGateContext requeue_gate;
    requeue_gate.action_type = action.type;
    requeue_gate.action_frame = action.Frame();
    requeue_gate.action_frame_count = action.FrameCount();
    requeue_gate.action_contact_frame = action.ContactFrame();
    requeue_gate.originating_command = currentAnim.originatingCommand;
    requeue_gate.command = command;
    requeue_gate.spatial_movement = spatialState.movement;
    requeue_gate.outgoing_movement = currentAnim.outgoingMovement;
    requeue_gate.incoming_movement = currentAnim.incomingMovement;
    requeue_gate.has_possession = HasPossession();
    requeue_gate.last_touch_tick = GetLastTouchTick();
    requeue_gate.last_touch_type = GetLastTouchType();
    requeue_gate.focus_distance =
        (tick.designated_possession_player->GetPosition() - spatialState.position)
            .GetLength();
    requeue_gate.requeue_delay_frames = humanoid->reQueueDelayFrames;
    requeue_gate.allow_movement_requeue = allowMovementReQueue;
    requeue_gate.allow_ball_control_requeue = allowBallControlReQueue;
    requeue_gate.allow_trap_requeue = allowTrapReQueue;
    requeue_gate.max_ball_control_requeue_frame = maxBallControlReQueueFrame;
    requeue_gate.min_remaining_movement_requeue_frames =
        minRemainingMovementReQueueFrames;
    requeue_gate.min_remaining_trap_requeue_frames =
        minRemainingTrapReQueueFrames;
    requeue_gate.now = now;
    if (!CanAttemptActionSelection(requeue_gate)) return false;
  }

  if (localInterruptAnim != e_InterruptAnim_ReQueue || action.Frame() > 12)
    humanoid->CalculateFactualSpatialState();

  assert(command.desiredLookAt.coords[2] == 0.0f);

  // CREATE A CRUDE SET OF POTENTIAL ANIMATIONS

  CrudeSelectionQuery query = BuildCrudeSelectionQuery(command, tick, now, currentMentalImage);

  DataSet dataSet;
  BakedAnimationSelector::CrudeSelection(
      animations_.Clips(), query, dataSet);
  if (dataSet.size() == 0) {
    if (command.desiredFunctionType == e_FunctionType_Movement) {
      dataSet.push_back(humanoid->GetIdleMovementAnimID());  // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
    } else
      return false;
  }

  // ANIM-1 A6.3b.2 / A6.4 shadow: keep the crude candidate set so the pure
  // Animation refinement can be compared with the legacy filter+sort result.
  DataSet crude_movement_snapshot;
  if (command.useDesiredMovement) crude_movement_snapshot = dataSet;
  // A6.4: the non-Movement branches set these (BallControl / Trap / Interfere).
  bool nm_strict = false;
  float nm_allowed_angle = 0.3f * pi;
  int nm_allowed_velocity_steps = 1;
  int nm_idle_level = 0;
  const bool pure_movement = command.desiredFunctionType == e_FunctionType_Movement &&
                             command.useDesiredMovement && !crude_movement_snapshot.empty();
  const bool pure_non_movement =
      command.desiredFunctionType != e_FunctionType_Movement &&
      command.useDesiredMovement && !crude_movement_snapshot.empty();

  if (command.useDesiredMovement) {
    Vector3 relDesiredDirection = command.desiredDirection.GetRotated2D(-spatialState.angle);
    float desiredAnimationVelocityFloat = command.desiredVelocityFloat;

    humanoid->SetMovementSimilarityPredicate(relDesiredDirection, FloatToEnumVelocity(desiredAnimationVelocityFloat));
    humanoid->SetBodyDirectionSimilarityPredicate(command.desiredLookAt);

    if (command.desiredFunctionType == e_FunctionType_Movement) {
      if (!pure_movement) {
        humanoid->_KeepBestDirectionAnims(dataSet, command, true);
        if (command.useDesiredLookAt) humanoid->_KeepBestBodyDirectionAnims(dataSet, command, true);
      }
    } else if (command.desiredFunctionType == e_FunctionType_BallControl) {
      bool strict = true;
      if (AllowLastDitch()) {
        strict = false;
      }
      float allowedBaseAngle = 0.0f * pi;
      int allowedVelocitySteps = 0;  // last ditch anims are always allowed > 0 velocity steps, as long as strict is false
      nm_strict = strict; nm_allowed_angle = allowedBaseAngle; nm_allowed_velocity_steps = allowedVelocitySteps;
      if (!pure_non_movement) {
        if (command.useDesiredLookAt) humanoid->_KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle);
        humanoid->_KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps);
      }
    } else if (command.desiredFunctionType == e_FunctionType_Trap) {
      bool strict = true;
      if (AllowLastDitch(false) || humanoid->_HighOrBouncyBall(tick.ball)) strict = false;
      float allowedBaseAngle = 0.3f * pi;
      int allowedVelocitySteps = 2;
      int bestBallControlQuadrantID = -1;
      nm_strict = strict; nm_allowed_angle = allowedBaseAngle; nm_allowed_velocity_steps = allowedVelocitySteps;
      if (!pure_non_movement) {
        humanoid->_KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps, bestBallControlQuadrantID);
        if (command.useDesiredLookAt) humanoid->_KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle);
      }

      // when too unlike command's desired movement, just don't go for it (and hope for another ballcontrol/trap anim to save us later on)
      if (!humanoid->_HighOrBouncyBall(tick.ball) && query.allowLastDitchAnims == false) {
        assert(!dataSet.empty());
        Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        const AnimationClip &bestWeGot = humanoid->GetBakedClip(*dataSet.begin());
        Vector3 bestWeGotMovement = bestWeGot.metadata.outgoing_movement.GetRotated2D(spatialState.angle);
        float currentDesiredDot = command.desiredDirection.GetDotProduct(spatialState.directionVec);

        bool allowAnim = true;

        radian angleDiff = std::fabs(bestWeGot.metadata.outgoing_direction.GetRotated2D(spatialState.angle).GetAngle2D(command.desiredDirection));
        if (angleDiff > 0.375f * pi) {
          allowAnim = false;
        }

        Vector3 desiredBestDiff = bestWeGotMovement - desiredMovement;
        if ((desiredBestDiff.GetLength() > walkVelocity + 0.5f &&
             currentDesiredDot > 0.0f) ||
            (desiredBestDiff.GetLength() > sprintVelocity + 0.5f &&
             currentDesiredDot <= 0.0f)) {
          allowAnim = false;
        }

        if (!allowAnim) {
          return false;
        }
      }
    } else if (command.desiredFunctionType == e_FunctionType_Interfere) {
      bool strict = false;
      float allowedAngle = 0.3f * pi;
      int allowedVelocitySteps = 1;
      if (command.strictMovement == e_StrictMovement_True) strict = true;
      nm_strict = strict; nm_allowed_angle = allowedAngle; nm_allowed_velocity_steps = allowedVelocitySteps;

      if (!pure_non_movement) {
        humanoid->_KeepBestDirectionAnims(dataSet, command, strict, allowedAngle, allowedVelocitySteps);
        if (command.useDesiredLookAt) humanoid->_KeepBestBodyDirectionAnims(dataSet, command, strict, allowedAngle);
      }
    }
  }

  if (!pure_movement && !pure_non_movement) {
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::ComparePriorityVariable, humanoid, _1, _2));

    int desiredIdleLevel = 0;
    if (!tick.play_authorized) desiredIdleLevel = 2;
    if (tick.set_piece_active) desiredIdleLevel = 1;
    else if ((tick.ball.Predict(football::sim::TickSpan{20}).position - spatialState.position).GetLength() > 16.0f) desiredIdleLevel = 1;
    humanoid->SetIdlePredicate(desiredIdleLevel);
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareIdleVariable, humanoid, _1, _2));

    // H3e4e2 measurement: strict counterfactual for the foot tie-break. The clone
    // is taken before the foot stable_sort and then receives exactly the same
    // remaining sorts, so the only difference between the two orders is foot.
    const bool foot_counterfactual =
        command.desiredFunctionType == e_FunctionType_Movement;
    DataSet withoutFootSort;
    if (foot_counterfactual) withoutFootSort = dataSet;
    humanoid->SetFootSimilarityPredicate(spatialState.foot);
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareFootSimilarity, humanoid, spatialState.foot, _1, _2));

    if (command.desiredFunctionType != e_FunctionType_BallControl) {
      humanoid->SetIncomingBodyDirectionSimilarityPredicate(spatialState.relBodyDirectionVec);
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareIncomingBodyDirectionSimilarity, humanoid, _1, _2));
    }

    // moved down
    humanoid->SetIncomingVelocitySimilarityPredicate(spatialState.enumVelocity);
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareIncomingVelocitySimilarity, humanoid, _1, _2));

    // OLD METHOD
    if (command.useDesiredTripDirection) {
      Vector3 relDesiredTripDirection = command.desiredTripDirection.GetRotated2D(-spatialState.angle);
      humanoid->SetTripDirectionSimilarityPredicate(relDesiredTripDirection);
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareTripDirectionSimilarity, humanoid, _1, _2));
    }

    // OLD METHOD
    if (command.desiredFunctionType != e_FunctionType_Movement) {
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareBaseanimSimilarity, humanoid, _1, _2));
    }

    if (command.desiredFunctionType == e_FunctionType_Deflect) {
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareCatchOrDeflect, humanoid, _1, _2));
    }

    if (foot_counterfactual) {
      // Same remaining chain, minus the foot sort. Conditions mirror production
      // so this stays a counterfactual and not a second scheduler.
      if (command.desiredFunctionType != e_FunctionType_BallControl) {
        humanoid->SetIncomingBodyDirectionSimilarityPredicate(spatialState.relBodyDirectionVec);
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&HumanoidBase::CompareIncomingBodyDirectionSimilarity, humanoid, _1, _2));
      }
      humanoid->SetIncomingVelocitySimilarityPredicate(spatialState.enumVelocity);
      std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&HumanoidBase::CompareIncomingVelocitySimilarity, humanoid, _1, _2));
      if (command.useDesiredTripDirection) {
        humanoid->SetTripDirectionSimilarityPredicate(command.desiredTripDirection.GetRotated2D(-spatialState.angle));
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&HumanoidBase::CompareTripDirectionSimilarity, humanoid, _1, _2));
      }
      if (command.desiredFunctionType != e_FunctionType_Movement) {
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&HumanoidBase::CompareBaseanimSimilarity, humanoid, _1, _2));
      }
      if (command.desiredFunctionType == e_FunctionType_Deflect) {
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&HumanoidBase::CompareCatchOrDeflect, humanoid, _1, _2));
      }
      if (!dataSet.empty() && !withoutFootSort.empty()) {
        RecordFootCounterfactual(animations_, *dataSet.begin(), *withoutFootSort.begin());
      }
    }
    MovementAnimationPerturbation &perturbation =
        MovementAnimationPerturbationAudit();
    if (perturbation.enabled && !perturbation.applied && foot_counterfactual &&
        localInterruptAnim == e_InterruptAnim_Switch &&
        action.type == e_FunctionType_Movement &&
        IsEligibleForProceduralLocomotion(tick.ball_retainer == this) &&
        !dataSet.empty() && !withoutFootSort.empty() &&
        dataSet.front() != withoutFootSort.front() &&
        (!perturbation.require_frame_count_difference ||
         humanoid->GetBakedClip(dataSet.front()).frame_count !=
             humanoid->GetBakedClip(withoutFootSort.front()).frame_count)) {
      perturbation.applied = true;
      perturbation.player_id = GetID();
      perturbation.time_ms = static_cast<int>(football::sim::ToMilliseconds(now));
      perturbation.original_anim_id = dataSet.front();
      perturbation.alternative_anim_id = withoutFootSort.front();
      // Only reorder the single branch's local candidate set. SelectAnim runs
      // once; the saved baseline branch independently runs the normal order.
      dataSet.swap(withoutFootSort);
    }
  } else if (pure_non_movement) {
    // ANIM-1 A6.4 step 1d: the pure Animation refinement is the authority for
    // the non-Movement candidate list; the legacy filter+sort chain no longer
    // runs for non-Movement commands that request desired movement. The
    // pre-removal dual-run comparison recorded `nm checks=4 match=4
    // mismatch=0`, and the A6.3a final-clip oracle still guards end-to-end.
    humanoid->SetIdlePredicate(nm_idle_level);
    humanoid->SetFootSimilarityPredicate(spatialState.foot);
    MovementRefinementRequest nm_request;
    nm_request.rel_desired_direction = humanoid->predicate_RelDesiredDirection;
    nm_request.desired_velocity = humanoid->predicate_DesiredVelocity;
    nm_request.cornering_bias = humanoid->predicate_CorneringBias;
    nm_request.look_at = humanoid->predicate_LookAt;
    nm_request.position = spatialState.position;
    nm_request.angle = spatialState.angle;
    nm_request.rel_incoming_body_direction = spatialState.relBodyDirectionVec;
    nm_request.incoming_velocity = spatialState.enumVelocity;
    nm_request.desired_foot = spatialState.foot;
    nm_request.idle_level = nm_idle_level;
    nm_request.use_desired_look_at = command.useDesiredLookAt;
    nm_request.is_movement = false;
    nm_request.is_ball_control =
        (command.desiredFunctionType == e_FunctionType_BallControl);
    const Vector3 nm_trip_direction =
        command.desiredTripDirection.GetRotated2D(-spatialState.angle);
    const std::vector<int> nm_pure = RefineNonMovementCandidates(
        animations_, crude_movement_snapshot, nm_request, nm_strict, nm_allowed_angle,
        nm_allowed_velocity_steps, command.useDesiredTripDirection, nm_trip_direction);
    dataSet.assign(nm_pure.begin(), nm_pure.end());
    ++LocomotionPlaybackAuditState().nm_pure_ticks;
  } else {
    // ANIM-1 A6.3b.2: the pure Animation refinement is the authority for the
    // Movement candidate list; the legacy chain no longer runs for Movement.
    MovementRefinementRequest refine_request;
    refine_request.rel_desired_direction = humanoid->predicate_RelDesiredDirection;
    refine_request.desired_velocity = humanoid->predicate_DesiredVelocity;
    refine_request.cornering_bias = humanoid->predicate_CorneringBias;
    refine_request.look_at = humanoid->predicate_LookAt;
    refine_request.position = spatialState.position;
    refine_request.angle = spatialState.angle;
    refine_request.rel_incoming_body_direction = spatialState.relBodyDirectionVec;
    refine_request.incoming_velocity = spatialState.enumVelocity;
    refine_request.desired_foot = spatialState.foot;
    int desiredIdleLevel = 0;
    if (!tick.play_authorized) desiredIdleLevel = 2;
    if (tick.set_piece_active) desiredIdleLevel = 1;
    else if ((tick.ball.Predict(football::sim::TickSpan{20}).position - spatialState.position).GetLength() > 16.0f) desiredIdleLevel = 1;
    refine_request.idle_level = desiredIdleLevel;
    refine_request.use_desired_look_at = command.useDesiredLookAt;
    refine_request.is_movement = true;

    // ANIM-1 A6.3b.3: the Player owns the Movement selection; the Humanoid
    // supplies the spatial request and reads back the chosen candidate list.
    dataSet = SelectMovement(animations_, crude_movement_snapshot,
                             refine_request);
    if (dataSet.empty()) dataSet.push_back(humanoid->GetIdleMovementAnimID());
    std::vector<int> without_foot = MovementCandidatesWithoutFoot();
    {  // ANIM-1 A6.3b.2: count the stateless refinement as the active Movement path.
      auto &refine_audit = LocomotionPlaybackAuditState();
      ++refine_audit.refine_checks;
      ++refine_audit.refine_matches;
    }
    if (!dataSet.empty() && !without_foot.empty()) {
      RecordFootCounterfactual(animations_, dataSet.front(), without_foot.front());
    }
    MovementAnimationPerturbation &perturbation = MovementAnimationPerturbationAudit();
    if (perturbation.enabled && !perturbation.applied &&
        localInterruptAnim == e_InterruptAnim_Switch &&
        action.type == e_FunctionType_Movement &&
        IsEligibleForProceduralLocomotion(tick.ball_retainer == this) &&
        !dataSet.empty() && !without_foot.empty() &&
        dataSet.front() != without_foot.front() &&
        (!perturbation.require_frame_count_difference ||
         humanoid->GetBakedClip(dataSet.front()).frame_count !=
             humanoid->GetBakedClip(without_foot.front()).frame_count)) {
      perturbation.applied = true;
      perturbation.player_id = GetID();
      perturbation.time_ms = static_cast<int>(football::sim::ToMilliseconds(now));
      perturbation.original_anim_id = dataSet.front();
      perturbation.alternative_anim_id = without_foot.front();
      dataSet.swap(without_foot);
    }
  }

  int selectedAnimID = -1;
  football::sim::TouchChoice touch_choice;

  if (dataSet.size() == 0 &&
      command.desiredFunctionType == e_FunctionType_Movement) {
    dataSet.push_back(humanoid->GetIdleMovementAnimID());  // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
  }

  Vector3 desiredBodyDirectionRel = Vector3(0, -1, 0);

  if (command.useDesiredLookAt) desiredBodyDirectionRel = (command.desiredLookAt - (spatialState.position + spatialState.movement * 0.1f)).GetNormalized(Vector3(0, -1, 0)).GetRotated2D(-spatialState.angle);

  // LOCO-2.3b: for pure procedural locomotion, reorder the already-valid
  // Movement candidates so the motion-driven strategy's preferred clip wins,
  // without adding candidates or touching the non-Movement/touch paths.
  if (command.desiredFunctionType == e_FunctionType_Movement &&
      LocomotionAnimationAdaptation() &&
      IsEligibleForProceduralLocomotion(tick.ball_retainer == this) &&
      LocomotionAnimationInputSnapshot().has_value() && !dataSet.empty()) {
    const auto choice = SelectLocomotionAnimation(
        *LocomotionAnimationInputSnapshot(), animations_.Clips());
    // LOCO-2.3c: record the reference rate for calibration; frameNum is not
    // advanced by it here.
    auto &audit = LocomotionPlaybackAuditState();
    ++audit.samples;
    audit.reference_speed_sum += choice.reference_speed;
    audit.playback_rate_sum += choice.playback_rate;
    audit.playback_rate_max = std::max(audit.playback_rate_max, choice.playback_rate);
    // ANIM-1 A6.3a shadow oracle: is the motion-driven choice a legal legacy
    // Movement candidate? A non-zero disagreement means the new selector would
    // pick a clip the legacy filter rejects, blocking the A6.3b hand-over.
    ++audit.shadow_checks;
    // A6.3b.1: the final pick rule (preferred if a legal candidate, else the
    // legacy first) is a pure Animation helper; the Humanoid only installs it.
    const AnimationId pick = ResolveMovementPick(dataSet, choice.animation_id);
    auto preferred = std::find(dataSet.begin(), dataSet.end(),
                               static_cast<int>(choice.animation_id));
    if (preferred != dataSet.end()) ++audit.shadow_in_candidates;
    else ++audit.shadow_not_in_candidates;
    auto picked = std::find(dataSet.begin(), dataSet.end(), static_cast<int>(pick));
    if (picked != dataSet.end() && picked != dataSet.begin())
      std::rotate(dataSet.begin(), picked, std::next(picked));
    // Final-result oracle: the Movement branch takes `*dataSet.begin()` after the
    // reorder, so this is the clip that would actually play.
    if (*dataSet.begin() == static_cast<int>(choice.animation_id)) {
      ++audit.clip_matches;
    } else {
      ++audit.clip_mismatches;
      ++audit.fallback_count;
    }
  }

  if (command.desiredFunctionType == e_FunctionType_Movement ||
      command.desiredFunctionType == e_FunctionType_Trip ||
      command.desiredFunctionType == e_FunctionType_Special) {
    selectedAnimID = *dataSet.begin();
    Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
    assert(desiredMovement.coords[2] == 0.0f);
    Vector3 physicsVector = humanoid->CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice.positions, touch_choice.rotation_smuggle);
    (void)physicsVector;
  } else if (command.desiredFunctionType == e_FunctionType_BallControl) {
    if (NeedTouch(now, tick, *dataSet.begin(), command, history)) {
      selectedAnimID = SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, preferPassAndShot, allowPreTouchRotationSmuggle, useContinuousBallCheck);
    }
  } else if (command.desiredFunctionType == e_FunctionType_Trap ||
             command.desiredFunctionType == e_FunctionType_Interfere ||
             command.desiredFunctionType == e_FunctionType_Deflect) {
    selectedAnimID = SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, preferPassAndShot, allowPreTouchRotationSmuggle, useContinuousBallCheck);
  } else if (command.desiredFunctionType == e_FunctionType_ShortPass ||
             command.desiredFunctionType == e_FunctionType_LongPass ||
             command.desiredFunctionType == e_FunctionType_HighPass ||
             command.desiredFunctionType == e_FunctionType_Shot) {
    selectedAnimID = SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, false, allowPreTouchRotationSmuggle, useContinuousBallCheck);
  } else if (command.desiredFunctionType == e_FunctionType_Sliding) {
    selectedAnimID = SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, false, allowPreTouchRotationSmuggle, useContinuousBallCheck);
    if (selectedAnimID == -1) {
      if (dataSet.size() > 0) {
        selectedAnimID = *dataSet.begin();
        Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        assert(desiredMovement.coords[2] == 0.0f);
        Vector3 physicsVector = humanoid->CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice.positions, touch_choice.rotation_smuggle);
        (void)physicsVector;
      }
    }
  }

  // check if we really want to requeue; if the anim we dug up is actually better than the current

  // ANIM-1 A6.4 step 6b: the post-search ReQueue gate is a Player-owned pure
  // decision. It runs at the exact same point as the legacy check, after the
  // candidate search that may contain conditional work and RNG.
  {
    football::sim::CommitSelectionContext commit_gate;
    commit_gate.is_requeue = (localInterruptAnim == e_InterruptAnim_ReQueue);
    commit_gate.has_selection = (selectedAnimID != -1);
    commit_gate.current_positions_sufficient = currentAnim.positions.size() > 1;
    commit_gate.selected_positions_sufficient = touch_choice.positions.size() > 1;
    commit_gate.action_type = action.type;
    commit_gate.command_type = command.desiredFunctionType;
    if (selectedAnimID != -1) {
      commit_gate.current_outgoing_velocity =
          FloatToEnumVelocity(humanoid->GetCurrentBakedClip().metadata.outgoing_velocity);
      commit_gate.selected_outgoing_velocity =
          FloatToEnumVelocity(humanoid->GetBakedClip(selectedAnimID).metadata.outgoing_velocity);
      commit_gate.current_quadrant = humanoid->GetCurrentBakedClip().metadata.quadrant;
      commit_gate.selected_quadrant = humanoid->GetBakedClip(selectedAnimID).metadata.quadrant;
      commit_gate.current_forced_outgoing_angle =
          ForceIntoPreferredDirectionAngle(
              humanoid->GetCurrentBakedClip().metadata.outgoing_angle);
      commit_gate.selected_forced_outgoing_angle =
          ForceIntoPreferredDirectionAngle(
              humanoid->GetBakedClip(selectedAnimID).metadata.outgoing_angle);
    }
    if (!CanCommitActionSelection(commit_gate)) selectedAnimID = -1;
  }

  // make it so

  if (selectedAnimID != -1) {
    // ANIM-1 A6.4 step 7b: selection and installation are separate units, and
    // the Player now dispatches the installation.
    return CommitSelectedAction(
        selectedAnimID, touch_choice, command, tick, now, history,
        localInterruptAnim, material_candidate);
  }

  return false;
}

// ---------------------------------------------------------------------------
// ANIM-1 A6.4 step 10: the retain-animation selection, relocated from the
// Humanoid so no selection path is left behind.
// ---------------------------------------------------------------------------
void Player::SelectRetainAnim() {
  Humanoid *const humanoid = CastHumanoid();
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  query.byOutgoingVelocity = true;
  query.outgoingVelocity = e_Velocity_Idle;
  query.properties.incoming_retain_state = "right_elbow";

  DataSet dataSet;
  BakedAnimationSelector::CrudeSelection(
      animations_.Clips(), query, dataSet);

  assert(dataSet.size() != 0);

  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareMovementSimilarity, humanoid, _1, _2));

  humanoid->startAngle = FixAngle((Vector3(0) - humanoid->startPos).GetAngle2D());  // 0.5 * pi; (facing right)

  humanoid->currentAnim.positions.clear();
  humanoid->currentAnim.animationId = *dataSet.begin();
  humanoid->currentAnim.frameNum = 0;
  humanoid->currentAnim.touchFrame = -1;
  humanoid->currentAnim.actionSmuggle = Vector3(0);
  humanoid->currentAnim.actionSmuggleOffset = Vector3(0);
  humanoid->currentAnim.actionSmuggleSustain = Vector3(0);
  humanoid->currentAnim.actionSmuggleSustainOffset = Vector3(0);
  humanoid->currentAnim.movementSmuggle = Vector3(0);
  humanoid->currentAnim.movementSmuggleOffset = Vector3(0);
  humanoid->currentAnim.rotationSmuggle.begin = 0;
  humanoid->currentAnim.rotationSmuggle.end = 0;
  humanoid->currentAnim.rotationSmuggleOffset = 0;
  humanoid->currentAnim.functionType = e_FunctionType_Movement;
  InstallSelectedAction(humanoid->CurrentSelectedAction());
}

// ---------------------------------------------------------------------------
// ANIM-1 A7 step 1: the Player owns the passive-trip decision. The guard is
// evaluated at contact time exactly as the legacy Humanoid::TripMe did; the
// accepted trip is applied to the Humanoid interrupt at the next Process.
// ---------------------------------------------------------------------------
void Player::TripMe(const Vector3 &tripVector, int tripType,
                    const Player *ball_retainer) {
  Humanoid *const humanoid = CastHumanoid();
  if (humanoid == nullptr) return;
  if (ball_retainer == this) return;
  const AnimationClip &clip = humanoid->GetCurrentBakedClip();
  football::sim::PassiveTripContext context;
  context.action_type = actionState.type;
  context.action_trip_type = clip.metadata.trip_type;
  context.incoming_special_state_empty =
      clip.metadata.incoming_special_state.empty();
  context.outgoing_special_state_empty =
      clip.metadata.outgoing_special_state.empty();
  context.interrupt_is_none =
      !pending_passive_trip_.has_value() &&
      humanoid->interruptAnim == e_InterruptAnim_None;
  context.requested_trip_type = tripType;
  if (!football::sim::CanAcceptPassiveTrip(context)) return;
  pending_passive_trip_ = PendingPassiveTrip{tripVector, tripType};
}

std::optional<PendingPassiveTrip> Player::ConsumePendingPassiveTrip() {
  std::optional<PendingPassiveTrip> trip = pending_passive_trip_;
  pending_passive_trip_.reset();
  return trip;
}

// ---------------------------------------------------------------------------
// ANIM-1 A7 step 2: the local trip selection queue is Player-owned. Relocated
// from HumanoidBase::AddTripCommandToQueue / GetTripCommand /
// GetBasicMovementCommand; the commands are byte-identical.
// ---------------------------------------------------------------------------
namespace {

PlayerCommand MakeTripCommand(const Vector3 &tripVector, int tripType,
                              float velocity) {
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Trip;
  command.useDesiredMovement = false;
  command.useDesiredTripDirection = true;
  command.desiredTripDirection = tripVector;
  command.desiredVelocityFloat = velocity;  // e_Velocity_Sprint;
  command.useTripType = true;
  command.tripType = tripType;
  return command;
}

}  // namespace

void Player::BuildTripSelectionCommands(PlayerCommandQueue &queue,
                                        const Vector3 &trip_direction,
                                        int trip_type,
                                        float current_velocity_float,
                                        const Vector3 &direction_vec,
                                        const Vector3 &position) const {
  if (trip_type == 1) {
    queue.push_back(MakeTripCommand(trip_direction, trip_type, current_velocity_float));
  } else {
    int otherTripType = 3;
    if (trip_type == 3) otherTripType = 2;
    queue.push_back(MakeTripCommand(trip_direction, trip_type, current_velocity_float));
    queue.push_back(MakeTripCommand(trip_direction, otherTripType, current_velocity_float));
    queue.push_back(MakeTripCommand(trip_direction, 1, current_velocity_float));
  }

  // backup, if there's no applicable trip anim
  PlayerCommand movement;
  movement.desiredFunctionType = e_FunctionType_Movement;
  movement.useDesiredMovement = true;
  movement.useDesiredLookAt = true;
  movement.desiredDirection = direction_vec;
  movement.desiredVelocityFloat = current_velocity_float;
  movement.desiredLookAt = position + movement.desiredDirection * 10.0f;
  queue.push_back(movement);
}
