// ANIM-1 A6.4 step 9a/10: the action-selection flow is Player-owned. This
// translation unit holds the relocated `SelectAnim` body, the crude-query
// builder and the `NeedTouch` gate. The Player owns the decision; the
// compatibility state only supplies the not-yet-migrated spatial facts.
//
// The pure scoring/refinement algorithms stay in Animation; the reference
// motion comes from the A8.0 PlayerMotion kernel. Nothing about candidate
// order, filter order, RNG timing or install timing changed.

#include "sim/player/player.hpp"

#include "football/animation/library.hpp"
#include "football/animation/movement_math.hpp"
#include "football/animation/selector.hpp"
#include "football/ball/ball.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player_touch_math.hpp"
#include "sim/player/player_motion.hpp"
#include "sim/player/locomotion_animation_strategy.hpp"
#include "sim/player/player_motion_constants.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <vector>

#include "football/animation/match.hpp"

using namespace football::math;
using std::placeholders::_1;
using std::placeholders::_2;

namespace {

// ANIM-2 A10.5: the candidate refinement never needs the motion port, so it
// gets an evaluator that is never called.
struct NoMotionEvaluator final : football::animation::MotionEvaluator {
  bool Evaluate(AnimationId, football::animation::MotionPlanView&) override {
    return false;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// ANIM-1 A6.4 step 9a: the crude candidate constraints, built by the Player.
// `CrudeSelectionQuery query;` is the first statement of the original block.
// ---------------------------------------------------------------------------
CrudeSelectionQuery Player::BuildCrudeSelectionQuery(
    const PlayerCommand &command, const football::sim::PlayerTickContext &tick,
    football::sim::Tick now, MentalImage *current_mental_image) {
  const SpatialState &spatialState = spatial_state_;
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
      GetCurrentBakedClip().metadata.outgoing_special_state.empty() &&
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

  query.properties.incoming_special_state = GetCurrentBakedClip().metadata.outgoing_special_state;
  if (tick.ball_retainer == this) query.properties.incoming_retain_state = GetCurrentBakedClip().metadata.outgoing_retain_state;
  if (command.useSpecialVar1) query.properties.special_var1 = command.specialVar1;
  if (command.useSpecialVar2) query.properties.special_var2 = command.specialVar2;

  if (!GetCurrentBakedClip().metadata.outgoing_special_state.empty()) query.incomingVelocity = e_Velocity_Idle;  // standing up anims always start out idle
  return query;
}

// ---------------------------------------------------------------------------
// ANIM-1 A6.4 step 10: the touch gate, relocated from Humanoid::NeedTouch.
// ---------------------------------------------------------------------------
bool Player::NeedTouch(football::sim::Tick now,
                       const football::sim::PlayerTickContext &tick, int animID,
                       const PlayerCommand &command,
                       std::span<MentalImage> history) {
  const SpatialState &spatialState = spatial_state_;

  // when idle (and desiredvelo is idle as well), don't want to touch the ball every frame

  const AnimationClip &clip = GetBakedClip(animID);

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity != e_Velocity_Idle)) return true;
  if (command.desiredVelocityFloat > idleDribbleSwitch) return true;
  if (std::fabs(tick.ball.state().velocity.GetLength()) > 2.0f) return true;

  Vector3 animMovement = clip.metadata.outgoing_movement.GetRotated2D(spatialState.angle) * 0.3f + spatialState.movement * 0.7f;

  float animVelo = animMovement.GetLength();
  animMovement.Normalize(spatialState.directionVec);
  animMovement *= spatialState.movement.GetLength() * 0.8f + animVelo * 0.2f;
  auto currentMentalImage =
      football::sim::observation::SampleMentalImage(history, mentalImageTime);
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
// Humanoid::SelectAnim; the compatibility state only supplies spatial facts.
// ---------------------------------------------------------------------------
bool Player::TrySelectAction(football::sim::Tick now,
                             const football::sim::PlayerTickContext &tick,
                             const PlayerCommand &command,
                             std::span<MentalImage> history,
                             e_InterruptAnim localInterruptAnim,
                             bool preferPassAndShot,
                             bool allowPreTouchRotationSmuggle,
                             bool useContinuousBallCheck) {
  const SpatialState &spatialState = spatial_state_;
  Anim &currentAnim = anim_state_;

  // returns false on no applicable anim found
  assert(command.desiredDirection.coords[2] == 0.0f);
  const PlayerActionState &action = actionState;
  const bool material_candidate =
      RecordSchedulerQuery(currentAnim.originatingCommand, command);

  // optimizations
  auto currentMentalImage = football::sim::observation::SampleMentalImage(
      history, mentalImageTime);
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
    // decision. The compatibility state supplies the not-yet-migrated legacy facts.
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
    requeue_gate.requeue_delay_frames = reQueueDelayFrames;
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
    ApplyFactualSpatialState();

  assert(command.desiredLookAt.coords[2] == 0.0f);

  // CREATE A CRUDE SET OF POTENTIAL ANIMATIONS

  CrudeSelectionQuery query = BuildCrudeSelectionQuery(command, tick, now, currentMentalImage);

  DataSet dataSet;
  BakedAnimationSelector::CrudeSelection(
      animations_.Clips(), query, dataSet);
  if (dataSet.size() == 0) {
    if (command.desiredFunctionType == e_FunctionType_Movement) {
      dataSet.push_back(SelectIdleMovementAnim(
          animations_, spatialState.position, spatialState.angle,
          spatialState.floatVelocity));  // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
    } else
      return false;
  }

  // ANIM-1 A6.3b.2 / A6.4 shadow: keep the crude candidate set so the pure
  // Animation refinement can be compared with the legacy filter+sort result.
  DataSet crude_movement_snapshot;
  if (command.useDesiredMovement) crude_movement_snapshot = dataSet;
  const bool pure_movement = command.desiredFunctionType == e_FunctionType_Movement &&
                             command.useDesiredMovement && !crude_movement_snapshot.empty();
  const bool pure_non_movement =
      command.desiredFunctionType != e_FunctionType_Movement &&
      command.useDesiredMovement && !crude_movement_snapshot.empty();

  // ANIM-2 A10.3: the resolved Movement/non-Movement request. The Player owns
  // these values; the filter/sort algorithms now live in Animation.
  const Vector3 relDesiredDirection = command.desiredDirection.GetRotated2D(-spatialState.angle);
  const e_Velocity desiredAnimationVelocity =
      FloatToEnumVelocity(command.desiredVelocityFloat);
  MovementRefinementRequest refine_request;
  refine_request.rel_desired_direction = relDesiredDirection;
  refine_request.desired_velocity = desiredAnimationVelocity;
  refine_request.cornering_bias = CalculateBiasForFastCornering(
      Vector3(0, -1.0f * spatialState.floatVelocity, 0),
      relDesiredDirection * EnumToFloatVelocity(desiredAnimationVelocity), 1.0f, 0.9f);
  refine_request.look_at = command.desiredLookAt;
  refine_request.position = spatialState.position;
  refine_request.angle = spatialState.angle;
  refine_request.rel_incoming_body_direction = spatialState.relBodyDirectionVec;
  refine_request.incoming_velocity = spatialState.enumVelocity;
  refine_request.desired_foot = spatialState.foot;
  refine_request.use_desired_look_at = command.useDesiredLookAt;
  refine_request.is_movement =
      (command.desiredFunctionType == e_FunctionType_Movement);
  refine_request.is_ball_control =
      (command.desiredFunctionType == e_FunctionType_BallControl);
  refine_request.idle_level = 0.0f;

  // ANIM-2 A10.3: SelectRetainAnim still sorts by the legacy movement
  // similarity, so keep the predicate values it reads exactly as the former
  // SetMovementSimilarityPredicate/SetBodyDirectionSimilarityPredicate did
  // (only when the command carried desired movement).
  if (command.useDesiredMovement) {
    predicate_RelDesiredDirection = relDesiredDirection;
    predicate_DesiredVelocity = desiredAnimationVelocity;
    predicate_CorneringBias = refine_request.cornering_bias;
  }

  // The legacy idle level (legacy + Movement paths).
  int desiredIdleLevel = 0;
  if (!tick.play_authorized) desiredIdleLevel = 2;
  if (tick.set_piece_active) desiredIdleLevel = 1;
  else if ((tick.ball.Predict(football::sim::TickSpan{20}).position - spatialState.position).GetLength() > 16.0f) desiredIdleLevel = 1;

  // Non-Movement strictness. It needs Player-owned facts (AllowLastDitch and
  // whether the ball is high/bouncy), so the Player resolves it and passes the
  // values in; the filtering itself is Animation's.
  bool nm_strict = false;
  float nm_allowed_angle = 0.3f * pi;
  int nm_allowed_velocity_steps = 1;
  if (command.useDesiredMovement) {
    if (command.desiredFunctionType == e_FunctionType_BallControl) {
      nm_strict = !AllowLastDitch();
      nm_allowed_angle = 0.0f * pi;
      nm_allowed_velocity_steps = 0;
    } else if (command.desiredFunctionType == e_FunctionType_Trap) {
      const bool high_or_bouncy = _HighOrBouncyBall(tick.ball);
      nm_strict = !(AllowLastDitch(false) || high_or_bouncy);
      nm_allowed_angle = 0.3f * pi;
      nm_allowed_velocity_steps = 2;

      // When too unlike the command's desired movement, just don't go for it
      // (and hope for another ballcontrol/trap anim to save us later on).
      if (!high_or_bouncy && query.allowLastDitchAnims == false) {
        assert(!dataSet.empty());
        Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        const AnimationClip &bestWeGot = GetBakedClip(*dataSet.begin());
        Vector3 bestWeGotMovement = bestWeGot.metadata.outgoing_movement.GetRotated2D(spatialState.angle);
        float currentDesiredDot = command.desiredDirection.GetDotProduct(spatialState.directionVec);
        bool allowAnim = true;
        radian angleDiff = std::fabs(bestWeGot.metadata.outgoing_direction.GetRotated2D(spatialState.angle).GetAngle2D(command.desiredDirection));
        if (angleDiff > 0.375f * pi) allowAnim = false;
        Vector3 desiredBestDiff = bestWeGotMovement - desiredMovement;
        if ((desiredBestDiff.GetLength() > walkVelocity + 0.5f &&
             currentDesiredDot > 0.0f) ||
            (desiredBestDiff.GetLength() > sprintVelocity + 0.5f &&
             currentDesiredDot <= 0.0f)) {
          allowAnim = false;
        }
        if (!allowAnim) return false;
      }
    } else if (command.desiredFunctionType == e_FunctionType_Interfere) {
      nm_strict = command.strictMovement == e_StrictMovement_True;
      nm_allowed_angle = 0.3f * pi;
      nm_allowed_velocity_steps = 1;
    }
  }

  const bool foot_counterfactual =
      command.desiredFunctionType == e_FunctionType_Movement;

  // ANIM-2 A10.5: the Player calls the single Animation::Match entry for the
  // candidate refinement; it no longer calls the refinement functions directly.
  football::animation::MatchQuery match_query;
  match_query.function_type = command.desiredFunctionType;
  match_query.constraints.strict = nm_strict;
  match_query.constraints.allowed_angle = nm_allowed_angle;
  match_query.constraints.allowed_velocity_steps = nm_allowed_velocity_steps;
  match_query.constraints.use_desired_trip_direction = command.useDesiredTripDirection;
  match_query.constraints.desired_trip_direction =
      command.desiredTripDirection.GetRotated2D(-spatialState.angle);
  match_query.foot_counterfactual = foot_counterfactual;
  if (!pure_movement && !pure_non_movement) {
    match_query.branch = football::animation::MatchBranch::Legacy;
    match_query.crude_candidates = dataSet;
    refine_request.idle_level = desiredIdleLevel;
  } else if (pure_non_movement) {
    match_query.branch = football::animation::MatchBranch::NonMovement;
    match_query.crude_candidates = crude_movement_snapshot;
  } else {
    match_query.branch = football::animation::MatchBranch::Movement;
    match_query.crude_candidates = crude_movement_snapshot;
    refine_request.idle_level = desiredIdleLevel;
  }
  match_query.refinement = refine_request;

  NoMotionEvaluator no_motion;
  const auto match =
      football::animation::Match(animations_, match_query, no_motion);
  DataSet withoutFootSort;
  if (match) {
    dataSet.assign(match->candidates.begin(), match->candidates.end());
    withoutFootSort = match->candidates_without_foot;
  } else {
    dataSet.clear();
  }

  if (!pure_movement && !pure_non_movement) {
    // ANIM-2 A10.3b: the legacy `useDesiredMovement == false` chain.
    if (foot_counterfactual && !dataSet.empty() && !withoutFootSort.empty()) {
      RecordFootCounterfactual(animations_, *dataSet.begin(), *withoutFootSort.begin());
    }
    MovementAnimationPerturbation &perturbation = MovementAnimationPerturbationAudit();
    if (perturbation.enabled && !perturbation.applied && foot_counterfactual &&
        localInterruptAnim == e_InterruptAnim_Switch &&
        action.type == e_FunctionType_Movement &&
        IsEligibleForProceduralLocomotion(tick.ball_retainer == this) &&
        !dataSet.empty() && !withoutFootSort.empty() &&
        dataSet.front() != withoutFootSort.front() &&
        (!perturbation.require_frame_count_difference ||
         GetBakedClip(dataSet.front()).frame_count !=
             GetBakedClip(withoutFootSort.front()).frame_count)) {
      perturbation.applied = true;
      perturbation.player_id = GetID();
      perturbation.time_ms = static_cast<int>(football::sim::ToMilliseconds(now));
      perturbation.original_anim_id = dataSet.front();
      perturbation.alternative_anim_id = withoutFootSort.front();
      dataSet.swap(withoutFootSort);
    }
  } else if (pure_non_movement) {
    // ANIM-2 A10.3b: the pure non-Movement refinement is the authority.
    ++LocomotionPlaybackAuditState().nm_pure_ticks;
  } else {
    // ANIM-2 A10.3a: the pure Movement refinement is the authority.
    if (dataSet.empty()) {
      dataSet.push_back(SelectIdleMovementAnim(
          animations_, spatialState.position, spatialState.angle,
          spatialState.floatVelocity));
    }
    {  // ANIM-1 A6.3b.2: count the stateless refinement as the active Movement path.
      auto &refine_audit = LocomotionPlaybackAuditState();
      ++refine_audit.refine_checks;
      ++refine_audit.refine_matches;
    }
    if (!dataSet.empty() && !withoutFootSort.empty()) {
      RecordFootCounterfactual(animations_, dataSet.front(), withoutFootSort.front());
    }
    MovementAnimationPerturbation &perturbation = MovementAnimationPerturbationAudit();
    if (perturbation.enabled && !perturbation.applied &&
        localInterruptAnim == e_InterruptAnim_Switch &&
        action.type == e_FunctionType_Movement &&
        IsEligibleForProceduralLocomotion(tick.ball_retainer == this) &&
        !dataSet.empty() && !withoutFootSort.empty() &&
        dataSet.front() != withoutFootSort.front() &&
        (!perturbation.require_frame_count_difference ||
         GetBakedClip(dataSet.front()).frame_count !=
             GetBakedClip(withoutFootSort.front()).frame_count)) {
      perturbation.applied = true;
      perturbation.player_id = GetID();
      perturbation.time_ms = static_cast<int>(football::sim::ToMilliseconds(now));
      perturbation.original_anim_id = dataSet.front();
      perturbation.alternative_anim_id = withoutFootSort.front();
      dataSet.swap(withoutFootSort);
    }
  }

  int selectedAnimID = -1;
  football::sim::TouchChoice touch_choice;

  if (dataSet.size() == 0 &&
      command.desiredFunctionType == e_FunctionType_Movement) {
    dataSet.push_back(SelectIdleMovementAnim(
          animations_, spatialState.position, spatialState.angle,
          spatialState.floatVelocity));  // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
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
    // legacy first) is a pure Animation helper; the Player only installs it.
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
    Vector3 physicsVector = CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice.positions, touch_choice.rotation_smuggle);
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
        Vector3 physicsVector = CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice.positions, touch_choice.rotation_smuggle);
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
          FloatToEnumVelocity(GetCurrentBakedClip().metadata.outgoing_velocity);
      commit_gate.selected_outgoing_velocity =
          FloatToEnumVelocity(GetBakedClip(selectedAnimID).metadata.outgoing_velocity);
      commit_gate.current_quadrant = GetCurrentBakedClip().metadata.quadrant;
      commit_gate.selected_quadrant = GetBakedClip(selectedAnimID).metadata.quadrant;
      commit_gate.current_forced_outgoing_angle =
          ForceIntoPreferredDirectionAngle(
              GetCurrentBakedClip().metadata.outgoing_angle);
      commit_gate.selected_forced_outgoing_angle =
          ForceIntoPreferredDirectionAngle(
              GetBakedClip(selectedAnimID).metadata.outgoing_angle);
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
  // ANIM-2 A10.3c: the clip choice is Animation's; the Player still owns the
  // action install and the physics-state writes below.
  const AnimationId retain_clip = SelectRetainAnimClip(
      animations_, predicate_RelDesiredDirection, predicate_DesiredVelocity,
      predicate_CorneringBias);

  startAngle = FixAngle((Vector3(0) - startPos).GetAngle2D());  // 0.5 * pi; (facing right)

  anim_state_.positions.clear();
  anim_state_.animationId = retain_clip;
  anim_state_.frameNum = 0;
  anim_state_.touchFrame = -1;
  anim_state_.actionSmuggle = Vector3(0);
  anim_state_.actionSmuggleOffset = Vector3(0);
  anim_state_.actionSmuggleSustain = Vector3(0);
  anim_state_.actionSmuggleSustainOffset = Vector3(0);
  anim_state_.movementSmuggle = Vector3(0);
  anim_state_.movementSmuggleOffset = Vector3(0);
  anim_state_.rotationSmuggle.begin = 0;
  anim_state_.rotationSmuggle.end = 0;
  anim_state_.rotationSmuggleOffset = 0;
  anim_state_.functionType = e_FunctionType_Movement;
  InstallSelectedAction(CurrentSelectedAction());
}

// ---------------------------------------------------------------------------
// ANIM-1 A7 step 1: the Player owns the passive-trip decision. The guard is
// evaluated at contact time exactly as the legacy Humanoid::TripMe did; the
// accepted trip is applied to the animation interrupt at the next action tick.
// ---------------------------------------------------------------------------
void Player::TripMe(const Vector3 &tripVector, int tripType,
                    const Player *ball_retainer) {
  if (ball_retainer == this) return;
  const AnimationClip &clip = GetCurrentBakedClip();
  football::sim::PassiveTripContext context;
  context.action_type = actionState.type;
  context.action_trip_type = clip.metadata.trip_type;
  context.incoming_special_state_empty =
      clip.metadata.incoming_special_state.empty();
  context.outgoing_special_state_empty =
      clip.metadata.outgoing_special_state.empty();
  context.interrupt_is_none =
      !pending_passive_trip_.has_value() &&
      interruptAnim == e_InterruptAnim_None;
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

// ---------------------------------------------------------------------------
// ANIM-1 A8 step 4: the Player computes and commits the non-procedural (baked
// action) motion. Relocated from the non-procedural branch of
// CalculateSpatialState; the compatibility spatial state is only a
// compatibility projection that this writes.
// ---------------------------------------------------------------------------
void Player::ComputeAndCommitActionMotion() {
  Anim &currentAnim = anim_state_;
  SpatialState &spatialState = spatial_state_;
  const AnimationClip &clip = GetCurrentBakedClip();

  ActionMotionVectorInput motion_input;
  motion_input.start_position = startPos;
  motion_input.start_angle = startAngle;
  motion_input.positions = currentAnim.positions;
  motion_input.frame_num = currentAnim.frameNum;
  motion_input.action_smuggle_offset = currentAnim.actionSmuggleOffset;
  motion_input.action_smuggle_sustain_offset = currentAnim.actionSmuggleSustainOffset;
  motion_input.movement_smuggle_offset = currentAnim.movementSmuggleOffset;
  if (currentAnim.positions.size() <=
      static_cast<std::size_t>(currentAnim.frameNum)) {
    motion_input.baked_pose_position =
        clip.poses[currentAnim.frameNum].positions[BodyPart::player];
  }
  motion_input.previous_position_2d = previousPosition2D;
  motion_input.action_smuggle_movement = spatialState.actionSmuggleMovement;
  motion_input.movement_smuggle_movement = spatialState.movementSmuggleMovement;
  motion_input.position_offset_movement = spatialState.positionOffsetMovement;
  motion_input.root_positions = clip.root_positions;

  const ActionMotionVectorResult motion =
      PlayerMotion::ActionMotionVector(motion_input);
  const Vector3 position = motion.position;

  if (currentAnim.frameNum > 12) {
    spatialState.foot = static_cast<e_Foot>(clip.metadata.outgoing_foot);
  }

  spatialState.actualMovement = motion.actual_movement;
  spatialState.physicsMovement = motion.physics_movement;
  spatialState.animMovement = motion.anim_movement;
  spatialState.movement = motion.movement;
  spatialState.floatVelocity = motion.float_velocity;
  spatialState.enumVelocity = motion.enum_velocity;
  spatialState.position = position;

  // The body direction / velocity quantization is PlayerMotion-owned too.
  ++HumanoidLegacyBodyPoseSamplesOnNonProceduralMovement();
  ActionBodyStateInput body_input;
  body_input.movement = spatialState.movement;
  body_input.start_angle = startAngle;
  body_input.rotation_smuggle_offset = currentAnim.rotationSmuggleOffset;
  body_input.baked_body_orientation =
      clip.poses[currentAnim.frameNum].orientations[body];
  const ActionBodyStateResult body_state =
      PlayerMotion::ActionBodyState(body_input);

  spatialState.directionVec = body_state.direction_vec;
  spatialState.angle = body_state.angle;
  spatialState.floatVelocity = body_state.float_velocity;
  spatialState.enumVelocity = body_state.enum_velocity;
  spatialState.relBodyDirectionVec = body_state.rel_body_direction_vec;
  spatialState.relBodyDirectionVecNonquantized =
      body_state.rel_body_direction_vec_nonquantized;
  spatialState.relBodyAngle = body_state.rel_body_angle;
  spatialState.relBodyAngleNonquantized = body_state.rel_body_angle_nonquantized;
  spatialState.bodyDirectionVec = body_state.body_direction_vec;
  spatialState.bodyAngle = body_state.body_angle;

  previousPosition2D = position;

  // The kinematic mirror must track the spatial state at the instant it changes.
  CommitLegacyActionMotion();
}

// ---------------------------------------------------------------------------
// ANIM-1 A8 step 5: the movement-smuggle calculation is Player-owned. Relocated
// line-for-line from Humanoid::CalculateMovementSmuggle.
// ---------------------------------------------------------------------------
namespace {
constexpr bool kEnableMovementSmuggle = true;
}  // namespace

Vector3 Player::ComputeMovementSmuggle(
    football::sim::Tick now, const football::sim::PlayerTickContext &tick,
    const Vector3 &desiredDirection, float desiredVelocityFloat,
    std::span<MentalImage> history) {
  Anim &currentAnim = anim_state_;

  if (!kEnableMovementSmuggle) return Vector3(0);

  if (tick.own_team.DesignatedPlayer() != this ||
      tick.designated_possession_player != this ||
      currentAnim.touchFrame != -1 ||
      (currentAnim.functionType == e_FunctionType_Trip &&
       GetCurrentBakedClip().metadata.trip_type != 1) ||
      GetCurrentBakedClip().metadata.incoming_special_state.compare("") != 0 ||
      GetCurrentBakedClip().metadata.outgoing_special_state.compare("") != 0 ||
      !tick.play_authorized || tick.set_piece_active || tick.ball_retainer != 0)
    return Vector3(0);

  Vector3 toDesired;

  // various stuff needed by all

  unsigned int timeToBall_ms = GetTimeNeededToGetToBall_ms();
  if (GetDesiredTimeToBall_ms() > (signed int)timeToBall_ms) {
    timeToBall_ms = GetDesiredTimeToBall_ms();
  }
  unsigned int animTime_ms =
      static_cast<int>(GetCurrentBakedClip().frame_count) * 10;
  unsigned int futureTime_ms = std::max(animTime_ms + defaultTouchOffset_ms, timeToBall_ms);

  Vector3 predictedOutgoingMovement =
      CalculateOutgoingMovement(currentAnim.positions);
  Vector3 predictedPos;
  radian predictedAngle;
  CalculatePredictedSituation(predictedPos, predictedAngle);
  Vector3 ballPos = football::sim::observation::SampleMentalImage(
                        history, mentalImageTime)
                        ->GetBallPrediction(futureTime_ms, now, tick.ball);
  float ballHeight = ballPos.coords[2];
  Vector3 ffo = GetFrontOfFootOffsetRel(
                    predictedOutgoingMovement.GetLength(),
                    GetCurrentBakedClip().metadata.outgoing_body_angle,
                    ballHeight)
                    .GetRotated2D(predictedAngle);
  Vector3 desiredBallPos = predictedPos + ffo;

  if (!HasPossession()) {
    // macro effect: consider a line going in the ball movement direction. consider the spot we want the ball at (in front of us) after this movement anim.
    // now calculate the shortest line between that line and that point. now move over that line from the point towards the line somewhat

    const Vector3 ballLineA =
        football::sim::observation::SampleMentalImage(history, mentalImageTime)
            ->GetBallPrediction(0, now, tick.ball)
            .Get2D();
    const Vector3 ballLineB =
        football::sim::observation::SampleMentalImage(history, mentalImageTime)
            ->GetBallPrediction(futureTime_ms, now, tick.ball)
            .Get2D();
    if ((ballLineA - ballLineB).GetLength() < 0.5f) return Vector3(0);  // ball is slow or very close

    float u = football::animation::SegmentProjectionParameter(ballLineA, ballLineB, desiredBallPos);
    Vector3 closestBallPos = ballLineA + (ballLineB - ballLineA) * u;

    toDesired = closestBallPos - desiredBallPos;

  } else {  // if HasPossession

    toDesired = ballPos.Get2D() - desiredBallPos;
  }

  unsigned int maxEffectTimeTreshold_ms = 250 + defaultTouchOffset_ms;  // if the ball is this much longer 'farther away' than feasible, rather postpone effect until next anim (else we may overrun)
  if (FloatToEnumVelocity(GetCurrentBakedClip().metadata.outgoing_velocity) == e_Velocity_Idle) maxEffectTimeTreshold_ms = 2000;  // no danger of overrunning
  float maxEffectVelocity = dribbleWalkSwitch;
  float maxSmuggleMPS = 1.6f;

  if (futureTime_ms - (animTime_ms + defaultTouchOffset_ms) > maxEffectTimeTreshold_ms) return Vector3(0);

  Vector3 toDesiredMovement = toDesired / (animTime_ms * 0.001f);
  Vector3 resultingMovement = predictedOutgoingMovement + toDesiredMovement;
  float predictedVelocity = predictedOutgoingMovement.GetLength();
  float resultingVelocity = resultingMovement.GetLength();
  if (resultingVelocity > predictedVelocity && resultingVelocity > maxEffectVelocity) return Vector3(0);

  toDesired.NormalizeMax(maxSmuggleMPS * ((static_cast<int>(GetCurrentBakedClip().frame_count) - 1) * 0.01f));

  // SetGreenDebugPilon(predictedPos);

  float removeDistance = 0.06f;  // remove part of the smuggle (allow staying this far away from ideal spot)
  toDesired = toDesired.GetNormalized(0) * std::max(0.0f, toDesired.GetLength() - removeDistance);
  return toDesired;
}

// ---------------------------------------------------------------------------
// ANIM-1 A8 step 5: the selection-time factual spatial reset, relocated from
// HumanoidBase::CalculateFactualSpatialState.
// ---------------------------------------------------------------------------
void Player::ApplyFactualSpatialState() {
  SpatialState &spatialState = spatial_state_;
  const AnimationClip &clip = GetCurrentBakedClip();

  spatialState.foot = static_cast<e_Foot>(clip.metadata.outgoing_foot);

  if (!clip.metadata.outgoing_special_state.empty()) {
    spatialState.floatVelocity = 0;
    spatialState.enumVelocity = e_Velocity_Idle;
    spatialState.movement = Vector3(0);
  }

  CommitLegacyActionMotion();
}

// ---------------------------------------------------------------------------
// ANIM-1 A9 step 3: the goalkeeper boundary clamps, relocated from
// the action tick. The retained goalkeeper must not leave the 16-meter box.
// ---------------------------------------------------------------------------
void Player::ApplyGoalkeeperBoundaryClamps(
    const football::sim::PlayerTickContext &tick, const Player *ball_retainer) {
  if (ball_retainer != this || GetFormationEntry().role != e_PlayerRole_GK ||
      !(tick.set_piece_active == false && tick.play_authorized == true)) {
    return;
  }

  if (tick.ball.state().position.coords[1] > 20.05f) {
    ApplyPositionCorrection(Vector3(0, clamp(20.05f - tick.ball.state().position.coords[1], -0.5f, 0.5f), 0) * 0.3f);
  }
  if (tick.ball.state().position.coords[1] < -20.05f) {
    ApplyPositionCorrection(Vector3(0, clamp(-20.05f - tick.ball.state().position.coords[1], -0.5f, 0.5f), 0) * 0.3f);
  }
  if (tick.ball.state().position.coords[0] * -GetDynamicSide() >
      -tick.pitch.half_length() + 16.4f) {
    ApplyPositionCorrection(Vector3(clamp((-tick.pitch.half_length() + 16.4f) -
                                     tick.ball.state().position.coords[0] *
                                         -GetDynamicSide(),
                                 -0.5f, 0.5f),
                           0, 0) *
                   -GetDynamicSide() * 0.3f);
  }
  if (tick.ball.state().position.coords[0] * -GetDynamicSide() <
      -tick.pitch.half_length() + 0.1f) {
    ApplyPositionCorrection(Vector3(clamp((-tick.pitch.half_length() + 0.1f) -
                                     tick.ball.state().position.coords[0] *
                                         -GetDynamicSide(),
                                 -0.5f, 0.5f),
                           0, 0) *
                   -GetDynamicSide() * 0.4f);
  }
}
