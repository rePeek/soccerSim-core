// ANIM-1 A9.6: the Player-owned action touch execution.
//
// The touch-request math and the per-action-type impulse publication used to be
// the second half of Humanoid::Process. They are moved here verbatim so the
// Player publishes touch facts without a Humanoid. The RNG draws (Shot error,
// Interfere direction, Deflect components), the touch order, the TouchSink /
// RuntimeSink call sites and the prepared-vs-direct publication distinction are
// unchanged.
#include "player/player.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include <bq_log/bq_log.h>

#include "football/animation/library.hpp"
#include "football/animation/movement_math.hpp"
#include "football/animation/touch_geometry.hpp"
#include "football/animation/touch_scoring.hpp"
#include "player/player_touch_event.hpp"
#include "player/player_touch_query.hpp"
#include "player/kick_targeting.hpp"
#include "player/player_active_touch_model.hpp"
#include "player/player_active_touch_shadow.hpp"
#include "player/player_control_builder.hpp"
#include "player/player_motion_constants.hpp"
#include "player/player_retain_anchor.hpp"
#include "player/player_runtime_sink.hpp"
#include "player/player_touch_math.hpp"

using namespace football::math;

namespace {

bool _PassFiddlingEnabled() {
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// ANIM-1 A9.6: the touch publication half of the action tick, relocated from
// Humanoid::Process. It returns the (possibly updated) ball retainer, because a
// Deflect may acquire the ball and a retain release may clear it.
// ---------------------------------------------------------------------------
Player* Player::ProcessActionTouches(football::sim::Tick now,
                                     const football::sim::PlayerTickContext& tick,
                                     const football::sim::player::PlayerObservation& observation,
                                     Player* ball_retainer,
                                     football::sim::player::PlayerTouchSink& /*touch_sink*/,
                                     football::sim::PlayerRuntimeSink& runtime_sink) {
  const PlayerActionState &action = GetSimulationActionState();
  std::optional<Vector3> prepared_velocity;
  std::optional<Vector3> prepared_anchor;
  const auto set_rotation = [&](radian, radian, radian, float) {
    // Legacy direct spin assignment is gone; spin comes from the point impulse.
  };
  // Tick-local publication: actors never reach the runtime owner for touch notification.
  std::optional<ActiveTouchObservation> active_observation;
  const auto begin_active_observation = [&](ActiveTouchOrigin origin) {
    if (!tick.active_touch_shadow) return;
    ActiveTouchObservation observation;
    const auto& state = this->GetSimulationActionState();
    observation.candidate.player = this->GetID();
    observation.candidate.action = state.type;
    const auto& part = GetCurrentBakedClip().metadata.touch_bodypart;
    observation.candidate.body_part = part.find("head") != std::string::npos ? PlayerBodyPart::Head :
        (part.find("foot") != std::string::npos || part.find("lowerleg") != std::string::npos ?
         PlayerBodyPart::LowerBody : PlayerBodyPart::UpperBody);
    observation.candidate.scheduled_tick = now + football::sim::TickSpan{1};
    observation.legacy_rule_tick = now;
    observation.animation_id = anim_state_.animationId;
    observation.contact_frame = state.ContactFrame();
    observation.elapsed = state.Frame();
    observation.origin = origin;
    observation.stage = ActiveTouchStage::NoImpulse;
    observation.before = tick.ball.state();
    observation.raw_animation_point = anim_state_.touchPos + anim_state_.positionOffset;
    observation.player_position = spatial_state_.position;
    active_observation = observation;
  };
  const auto flush_active_observation = [&] {
    if (!active_observation) return;
    active_observation->after = tick.ball.state();
    active_observation->candidate.target_velocity = active_observation->after.velocity;
    active_observation->candidate.target_angular_velocity = active_observation->after.angular_velocity;
    tick.active_touch_shadow->Observe(*active_observation);
    active_observation.reset();
  };
  const auto apply_touch = [&](const Vector3& velocity) {
    prepared_velocity = velocity;
  };
  const auto notify_touch = [&](e_TouchType type) {
    if (tick.touch_preparation) {
      if (prepared_velocity) {
        const auto& part = GetCurrentBakedClip().metadata.touch_bodypart;
        const auto body_part = part.find("head") != std::string::npos ? PlayerBodyPart::Head :
            (part.find("foot") != std::string::npos || part.find("lowerleg") != std::string::npos ?
             PlayerBodyPart::LowerBody : PlayerBodyPart::UpperBody);
        // P4f-3: capture the real boot surface point nearest the ball. Take
        // BOTH boot capsules and keep the nearer one: the kicking boot is the
        // one at the ball at the contact frame while the planted support boot
        // is elsewhere. `current_foot` is selector provenance, not a reliable
        // contact-foot identity in the shipped asset.
        football::math::Vector3 strike_point;
        float strike_radius = 0.0f;
        int strike_foot = -1;
        {
          const auto sample = this->GetBakedPoseSample();
          const auto regions = sample.ContactRegions();
          const auto& ball_center = tick.ball.state().position;
          const auto surface = [&](const auto& boot) {
            const auto axis = boot.b - boot.a;
            const float len2 = axis.GetDotProduct(axis);
            const float t = len2 > 0 ? clamp((ball_center - boot.a).GetDotProduct(axis)/len2, 0.0f, 1.0f) : 0.0f;
            const auto segment = boot.a + axis * t;
            const auto delta = ball_center - segment;
            const float dist = delta.GetLength();
            return dist > 1e-6f ? segment + delta * (boot.radius/dist) : segment;
          };
          const auto& left = std::get<football::geometry::Capsule>(regions[1]);
          const auto& right = std::get<football::geometry::Capsule>(regions[2]);
          const auto lp = surface(left), rp = surface(right);
          const float ld = (lp - ball_center).GetLength();
          const float rd = (rp - ball_center).GetLength();
          strike_foot = ld <= rd ? 0 : 1;
          const auto& boot = ld <= rd ? left : right;
          strike_point = ld <= rd ? lp : rp;
          strike_radius = boot.radius;
        }
        tick.touch_preparation->PrepareTouch({
            {now, this->GetID(), this->GetTeamSide(), type, tick.ball.state().position,
             *prepared_velocity, static_cast<int>(this->GetSimulationActionState().type)},
            body_part, prepared_anchor.value_or(anim_state_.touchPos + anim_state_.positionOffset),
            *prepared_velocity, prepared_anchor.has_value(),
            strike_point, strike_radius, strike_foot,
            anim_state_.animationId,
            this->GetSimulationActionState().ContactFrame(),
            this->GetSimulationActionState().Frame()});
      }
      prepared_velocity.reset();
      prepared_anchor.reset();
      return;
    }
  };
  // Prepared execution never commits physics here. The default fused path
  // retains its legacy touch until the reviewed default/Golden transition.
  const auto physical_touch = [&](e_FunctionType, const Vector3& touchVec) -> bool {
    if (!tick.touch_preparation) return false;
    apply_touch(touchVec);
    return true;
  };
  float ballDistanceNow = (tick.ball.state().position.Get2D() - spatial_state_.position).GetLength();
  float ballDistanceFuture = (tick.ball.Predict(football::sim::TickSpan{20}).position.Get2D() - (spatial_state_.position + spatial_state_.movement * 0.2f)).GetLength();
  float lastTouchBias = this->GetLastTouchBias(1500, now);
  float oppLastTouchBias = football::sim::player::TeamTouchBias(tick.touches,
      tick.opponent_team.team_id, tick.opponent_team.teammates, 240, now);

  if (this == tick.designated_possession_player &&
      ((lastTouchBias <= 0.01f && oppLastTouchBias <= 0.01f &&
        action.type == e_FunctionType_Movement &&
        ballDistanceNow < 0.6f && ballDistanceFuture > 0.65f &&
        ballDistanceFuture > ballDistanceNow)  // 0.5 / 0.6

       ||

       (lastTouchBias <= 0.7f && this->HasPossession() &&
        action.type == e_FunctionType_Trip &&
        ballDistanceNow < 0.4f)

       ) &&
      tick.ball.state().position.coords[2] < 1.6f) {

    this->TriggerControlledBallCollision();
    //SetGreenDebugPilon(spatial_state_.position);
  }

  // ------------------------ EXPERIMENTAL ------------------------------------------------
  bool controlledBallCollision = this->IsControlledBallCollisionTriggered();
  if (controlledBallCollision) this->ResetControlledBallCollisionTrigger();
  const bool may_touch = tick.ball_in_play ||
      (tick.play_authorized && tick.set_piece_active && tick.restart.active &&
       tick.restart.taker == this);
  if (may_touch && controlledBallCollision && !action.HasScheduledContact()) {
    begin_active_observation(ActiveTouchOrigin::Controlled);
    if (active_observation) active_observation->candidate.reachable = true;
    Vector3 currentBallVec = tick.ball.state().velocity;
    radian nextBodyAngle = startAngle + GetCurrentBakedClip().metadata.outgoing_angle + GetCurrentBakedClip().metadata.outgoing_body_angle + anim_state_.rotationSmuggle.end;

    radian xRot = 0;
    radian yRot = 0;
    Vector3 touchVec = GetTrapVector(&tick.ball, this, tick.touches, tick.opponent_team, now, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(anim_state_.positions), anim_state_, anim_state_.frameNum, spatial_state_, decayingPositionOffset, xRot, yRot);
    if (anim_state_.originatingCommand.modifier &
        e_PlayerCommandModifier_KnockOn) {
      touchVec *= 1.35f;//1.2f;
    }

    float bumpyRideBias = 0.0f;
    touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

    apply_touch(touchVec);
    set_rotation(xRot, yRot, 0, 0.2f * (1.0f - bumpyRideBias)); // 0.9
    notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart)); //, e_TouchType_Accidental
  }
  // ---------------------- / EXPERIMENTAL ------------------------------------------------

  if (action.HasScheduledContact() && action.Frame() <= action.ContactFrame()) {
    begin_active_observation(ActiveTouchOrigin::Scheduled);
    if (active_observation) {
      active_observation->candidate.scheduled_tick = now + football::sim::TickSpan{
          static_cast<std::uint64_t>(action.ContactFrame() - action.Frame() + 1)};
      active_observation->stage = action.Frame() < action.ContactFrame() ?
          ActiveTouchStage::Pending : ActiveTouchStage::NoImpulse;
      if (!may_touch && action.Frame() == action.ContactFrame()) {
        active_observation->stage = ActiveTouchStage::Rejected;
        active_observation->rejection = ActiveTouchReject::Authorization;
      }
    }
  }
  if (may_touch && action.HasScheduledContact() &&
      action.Frame() == action.ContactFrame()) {
    ContactAuthorityAudit *contact_audit =
        ContactAuthorityAuditEnabled() && IsTrackedScheduledContact(action.type)
        ? &ContactAuthorityFor(action.type) : nullptr;
    bool contact_impulse_generated = false;
    if (contact_audit) ++contact_audit->at_contact_frame;
    if (contact_audit) {
      contact_audit->observed_elapsed_ms.push_back(static_cast<int>(football::sim::ToMilliseconds(action.elapsed)));
      if (anim_state_.positionOffset.GetLength() > 0.0f)
        ++contact_audit->contact_position_offset_nonzero;
      if (!anim_state_.positions.empty())
        ++contact_audit->animation_positions_present;
    }

    Vector3 desiredBallPosition;
    for (const auto& t : GetCurrentBakedClip().touches) {
      if (t.frame == action.ContactFrame()) desiredBallPosition = t.position;
    }
    float desiredBallHeight = desiredBallPosition.coords[2];
    if (contact_audit)
      contact_audit->desired_ball_heights.push_back(desiredBallHeight);

    float touchableDistance = 0.4f;

    float fullBallDistance = (tick.ball.state().position - (anim_state_.touchPos + anim_state_.positionOffset)).GetLength();

    if (!GetCurrentBakedClip().metadata.incoming_retain_state.empty()) {
      fullBallDistance = 0.0f;
      touchableDistance = 1.0f;
    }

    float bumpyRideBias = fullBallDistance / touchableDistance;
    bumpyRideBias = clamp(bumpyRideBias - 0.001f, 0.0f, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 0.5f);
    Vector3 currentBallVec = tick.ball.state().velocity;
    bool contact_reachable = false;
    if (active_observation) {
      active_observation->legacy_reach_evaluated = true;
      active_observation->candidate.reach_error = fullBallDistance;
      active_observation->incoming_retain_override = !GetCurrentBakedClip().metadata.incoming_retain_state.empty();
      active_observation->candidate.reachable = fullBallDistance < touchableDistance &&
          std::fabs(desiredBallHeight - tick.ball.state().position.coords[2]) < 1.0f;
      if (!active_observation->candidate.reachable) {
        active_observation->stage = ActiveTouchStage::Rejected;
        active_observation->rejection = fullBallDistance < touchableDistance ?
            ActiveTouchReject::Height : ActiveTouchReject::Distance;
      }
    }
    if (contact_audit) {
      contact_audit->full_ball_distances.push_back(fullBallDistance);
      contact_audit->bumpy_ride_biases.push_back(bumpyRideBias);
      const float height_gap = std::fabs(
          desiredBallHeight - tick.ball.state().position.coords[2]);
      if (!GetCurrentBakedClip().metadata.incoming_retain_state.empty())
        ++contact_audit->incoming_retain_override;
      if (!(fullBallDistance < touchableDistance)) {
        ++contact_audit->distance_rejected;
      } else if (!(height_gap < 1.0f)) {
        ++contact_audit->height_rejected;
      } else {
        ++contact_audit->physically_reachable;
        contact_reachable = true;
      }
    }
    const auto record_contact_impulse = [&](const Vector3 &impulse) {
      if (!contact_audit) return;
      contact_impulse_generated = true;
      ++contact_audit->impulse_calls;
      const float speed = impulse.GetLength();
      contact_audit->impulse_request_speeds.push_back(speed);
      if (speed > 0.0f) ++contact_audit->nonzero_impulse_requests;
    };

    if (fullBallDistance < touchableDistance &&
        std::fabs(desiredBallHeight - tick.ball.state().position.coords[2]) <
            1.0f) {

      radian nextBodyAngle = startAngle + GetCurrentBakedClip().metadata.outgoing_angle + GetCurrentBakedClip().metadata.outgoing_body_angle + anim_state_.rotationSmuggle.end;

      if (anim_state_.functionType == e_FunctionType_Trap ||
          (anim_state_.functionType == e_FunctionType_BallControl &&
           this->HasPossession() == false)) {
        //printf("trap!\n");
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetTrapVector(&tick.ball, this, tick.touches, tick.opponent_team, now, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(anim_state_.positions), anim_state_, anim_state_.frameNum, spatial_state_, decayingPositionOffset, xRot, yRot);
        if (anim_state_.originatingCommand.modifier &
            e_PlayerCommandModifier_KnockOn) {
          touchVec *= 1.35f;
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        const bool physical = physical_touch(anim_state_.functionType, touchVec);
        if (!physical) {
          apply_touch(touchVec);
          set_rotation(xRot, yRot, 0, 0.5f * (1.0f - bumpyRideBias));
        }
        record_contact_impulse(touchVec);

        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (anim_state_.functionType == e_FunctionType_BallControl) {
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetBallControlVector(&tick.ball, this, tick.opponent_team, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(anim_state_.positions), anim_state_, anim_state_.frameNum, spatial_state_, decayingPositionOffset, xRot, yRot);
        if (anim_state_.originatingCommand.modifier &
            e_PlayerCommandModifier_KnockOn) {
          touchVec *= 1.35f;
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        apply_touch(touchVec);
        record_contact_impulse(touchVec);
        set_rotation(xRot, yRot, 0, 0.6f * (1.0f - bumpyRideBias)); // 1.0

        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (anim_state_.functionType == e_FunctionType_ShortPass ||
               anim_state_.functionType == e_FunctionType_LongPass ||
               anim_state_.functionType == e_FunctionType_HighPass) {

        Vector3 ballDirection = anim_state_.originatingCommand.touchInfo.desiredDirection;
        float ballPower = anim_state_.originatingCommand.touchInfo.desiredPower;
        Player *targetPlayer = anim_state_.originatingCommand.touchInfo.targetPlayer;
        Vector3 inputDirection = anim_state_.originatingCommand.touchInfo.inputDirection;

        // refine/change target, if new target is close enough to old target

        //targetPlayer = 0;//anim_state_.originatingCommand.touchInfo.targetPlayer;
        Vector3 tmpBallDirection = ballDirection;
        float tmpBallPower = ballPower;
        Player *tmpTargetPlayer = 0;
        Player *forcedTargetPlayer = 0;
        football::sim::mechanics::GetPass(this, tick.own_team.teammates, anim_state_.originatingCommand.desiredFunctionType, inputDirection, anim_state_.originatingCommand.touchInfo.inputPower, anim_state_.originatingCommand.touchInfo.autoDirectionBias, anim_state_.originatingCommand.touchInfo.autoPowerBias, tmpBallDirection, tmpBallPower, tmpTargetPlayer, anim_state_.originatingCommand.touchInfo.forcedTargetPlayer);
        float maxDeviationAngle = 0.15f * pi;
        radian angleDiff = tmpBallDirection.Get2D().GetAngle2D(ballDirection.Get2D());
        if (std::fabs(angleDiff) <= maxDeviationAngle) {
          ballDirection = tmpBallDirection;
          ballPower = tmpBallPower;
          targetPlayer = tmpTargetPlayer;
        } else if (std::fabs(angleDiff) < 2.0f * maxDeviationAngle) {
          // get as close as possible
          float clampedAngleDiff = clamp(angleDiff, -maxDeviationAngle, maxDeviationAngle);
          ballDirection = ballDirection.GetRotated2D(clampedAngleDiff);

          if (tmpTargetPlayer != targetPlayer) {
            // if we can't make it to our refined target at all, just stick with original ballpower (think about refined target at ~180 deg, would be weird to pass forward with the power of that (unreachable) target)
            float refinedBias = NormalizedClamp(std::fabs(clampedAngleDiff), 0.0f, std::fabs(angleDiff));
            ballPower = ballPower * (1.0f - refinedBias) + tmpBallPower * refinedBias;
            targetPlayer = tmpTargetPlayer; // new, and removed line below
          } else {
            ballPower = tmpBallPower;
          }

        }  // else: just stick to original

        if (targetPlayer) {
          runtime_sink.SetDesignatedPlayer(GetTeamID(), targetPlayer);
        }
        float zcurve = 0.0f;
        Vector3 touchVec = ballDirection * 36 * (ballPower + 0.3f);

        if (_PassFiddlingEnabled()) {
          if (contact_audit) ++contact_audit->pass_fiddling;
          //SetGreenDebugPilon(tick.ball.state().position.Get2D() + touchVec.Get2D() * 0.4f);

          touchVec = GetBestPossibleTouch(now, tick, touchVec, anim_state_.functionType);

          // add a little curve for aesthetics & realism
          radian bodyTouchAngle = spatial_state_.bodyDirectionVec.GetAngle2D(touchVec) / pi;
          if (std::fabs(bodyTouchAngle) > 0.5f) bodyTouchAngle = (1.0f - std::fabs(bodyTouchAngle)) * signSide(bodyTouchAngle);
          bodyTouchAngle *= 2.0f;
          //printf("bodyTouchAngle: %f\n", bodyTouchAngle);
          radian amount = bodyTouchAngle * 0.25f;
          if (anim_state_.functionType == e_FunctionType_HighPass) amount *= 0.2f;
          touchVec.Rotate2D(amount * (0.4f + 0.6f * NormalizedClamp(touchVec.GetLength(), 0.0f, 70.0f)));
          zcurve = amount * -340;//-600;

          //SetRedDebugPilon(tick.ball.state().position.Get2D() + touchVec.Get2D() * 0.4f);
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        const bool physical = physical_touch(anim_state_.functionType, touchVec);
        if (!physical) apply_touch(touchVec);
        record_contact_impulse(touchVec);
        float forwardness = 3.5f;
        if (anim_state_.functionType == e_FunctionType_HighPass) forwardness = -1.3f;
        radian xRot = touchVec.GetNormalized(0).coords[1] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        radian yRot = touchVec.GetNormalized(0).coords[0] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        if (!physical) set_rotation(xRot, yRot, zcurve, 0.9f * (1.0f - bumpyRideBias));

        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (anim_state_.functionType == e_FunctionType_Shot) {

        // alter direction, if needed
        Vector3 ballDirection = anim_state_.originatingCommand.touchInfo.desiredDirection;
        Vector3 inputDirection = anim_state_.originatingCommand.touchInfo.inputDirection;
        Vector3 ballDirectionAltered = football::sim::mechanics::GetShotDirection(this, tick.pitch, inputDirection, anim_state_.originatingCommand.touchInfo.autoDirectionBias);

        float maxDeviationAngle = 0.1f * pi;
        radian angleDiff = ballDirectionAltered.Get2D().GetAngle2D(ballDirection.Get2D());
        if (std::fabs(angleDiff) > maxDeviationAngle) {
          // get as close as possible
          float clampedAngleDiff = clamp(angleDiff, -maxDeviationAngle, maxDeviationAngle);
          ballDirection = ballDirection.GetRotated2D(clampedAngleDiff);
        } else {
          ballDirection = ballDirectionAltered;
        }

        radian xRot = 0;
        radian yRot = 0;
        radian zRot = 0;
        Vector3 touchVec = GetShotVector(&tick.ball, this, GetCurrentBakedClip(), anim_state_, spatial_state_, decayingPositionOffset, xRot, yRot, zRot);

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        const bool physical = physical_touch(anim_state_.functionType, touchVec);
        if (!physical) {
          apply_touch(touchVec);
          set_rotation(xRot, yRot, zRot, 0.7f * (1.0f - bumpyRideBias));
        }
        record_contact_impulse(touchVec);
        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (anim_state_.functionType == e_FunctionType_Interfere) {
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetTrapVector(&tick.ball, this, tick.touches, tick.opponent_team, now, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(anim_state_.positions), anim_state_, anim_state_.frameNum, spatial_state_, decayingPositionOffset, xRot, yRot);
        touchVec =
            touchVec * 0.5f +
            (tick.ball.state().position.Get2D() - spatial_state_.position)
                    .GetNormalized() *
                4.0f +
            Vector3(0, 0, 0.5f + 0.5f * NormalizedClamp(tick.ball.state().velocity.GetLength(), 0.0f, 10.0f));  // was random 1 .. 6

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        apply_touch(touchVec);
        // Legacy three-argument call: the third value is z rotation; bias was 1.0.
        set_rotation(xRot, yRot, 0.3f * (1.0f - bumpyRideBias), 1.0f);
        notify_touch(e_TouchType_Accidental); // it's not truly accidental, but the resulting direction somewhat is, so goalies may fetch these balls
      }

      else if (anim_state_.functionType == e_FunctionType_Deflect) {
        bool canRetain = true; // can we grab hold of the ball?
        if (GetCurrentBakedClip().metadata.outgoing_retain_state.compare("") == 0) canRetain = false; // not the right anim, hopeless!
        if (ball_retainer != 0) canRetain = false; // somebody is already holding the ball :( (dafuq, this should not happen, right?)

        float veloDifficulty = NormalizedClamp((tick.ball.state().velocity - spatial_state_.movement).GetLength(), 0.0f, 40.0f);
        float reactionDifficulty = 0.0f;
        Player *lastTouchPlayer = football::sim::player::LastTouchPlayer(tick.touches,
            tick.opponent_team.team_id, tick.opponent_team.teammates);
        if (lastTouchPlayer) {
          reactionDifficulty =
              std::pow(lastTouchPlayer->GetLastTouchBias(
                           1200 - GetStat(football::player::PlayerStat::physical_reaction) * 400, now),
                       0.6f);
        }
        if ((1.0f - veloDifficulty) * (1.0f - reactionDifficulty) < 0.3f) canRetain = false; // too hard!

        if (canRetain) {
          if (active_observation) {
            active_observation->origin = ActiveTouchOrigin::RetainAcquire;
            active_observation->stage = ActiveTouchStage::Constraint;
          }
          runtime_sink.SetBallRetainer(this);
          ball_retainer = this;
        } else {
          Vector3 currentBallMovement = tick.ball.state().velocity.Get2D();
          Vector3 playerMovement = spatial_state_.movement;
          // Deterministic scatter side: the ball leaves to the side it already
          // sits on relative to the body, so the same contact repeats.
          const Vector3 toBall2D =
              tick.ball.state().position.Get2D() - spatial_state_.position;
          const float scatterSide = signSide(toBall2D.GetDotProduct(
              Vector3(-spatial_state_.directionVec.coords[1],
                      spatial_state_.directionVec.coords[0], 0.0f)));
          Vector3 touchVec =
              (-currentBallMovement * 0.1f + playerMovement * 2.0f +
               Vector3(-this->GetExecutionDirection(), 0, 0) * 4.0f +
               Vector3(0, scatterSide, 0))
                  .GetNormalized(0) *
              (currentBallMovement.GetLength() * 0.3f +
               playerMovement.GetLength() * 2.5f);
          touchVec.coords[2] += 1.2f;

          touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

          apply_touch(touchVec);
          set_rotation(0, 0, 0, 0.2f * (1.0f - bumpyRideBias));
        }
        notify_touch(e_TouchType_Accidental);
      }

      else if (anim_state_.functionType == e_FunctionType_Sliding) {
        Vector3 touchVec = GetCurrentBakedClip().metadata.outgoing_ball_direction.GetRotated2D(spatial_state_.angle);
        touchVec = touchVec * 6.0f + tick.ball.state().velocity * -0.28f;
        touchVec += Vector3(0, 0, 6);

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        apply_touch(touchVec);

        notify_touch(e_TouchType_Accidental);
      }
    }
    if (contact_audit && !contact_impulse_generated) {
      ++contact_audit->suppressed;
      if (contact_reachable) ++contact_audit->reachable_without_impulse;
    }
  }
  flush_active_observation();

  if (ball_retainer == this) {
    if (((!action.HasScheduledContact() ||
          action.Frame() >= action.ContactFrame()) &&
         GetCurrentBakedClip().metadata.outgoing_retain_state != "") ||
        (action.HasScheduledContact() &&
         action.Frame() < action.ContactFrame() &&
         GetCurrentBakedClip().metadata.incoming_retain_state != "") ||
        (GetCurrentBakedClip().metadata.incoming_retain_state != "" &&
         GetCurrentBakedClip().metadata.outgoing_retain_state != "")) {
      // Anchor the ball to a body-semantic local offset. The retain state
      // string decides which anchor; the position itself is a pure function of
      // simulation state, so it no longer depends on when the animation pose
      // was last refreshed.
      std::string retainState =
          GetCurrentBakedClip().metadata.outgoing_retain_state;
      if (retainState.empty()) {
        retainState = GetCurrentBakedClip().metadata.incoming_retain_state;
      }
      RetainAnchorKind anchor;
      if (!ParseRetainAnchorKind(retainState, anchor)) {
        // Fail fast: a silent fallback would place the retained ball
        // somewhere plausible but wrong.
        throw std::runtime_error("Player::ProcessActionTouches: unknown retain state: " +
                                 retainState);
      }
      begin_active_observation(ActiveTouchOrigin::RetainAnchor);
      apply_touch(Vector3(0));
      if (active_observation) active_observation->stage = ActiveTouchStage::Constraint;
      set_rotation(0, 0, 0, 1.0);
      const auto anchor_position = ComputeRetainAnchor(
          spatial_state_.position, spatial_state_.bodyDirectionVec, anchor);
      prepared_anchor = anchor_position;
      notify_touch(e_TouchType_Intentional_Nonkicked);
    } else {
      // no longer retaining
      begin_active_observation(ActiveTouchOrigin::RetainRelease);
      if (active_observation) active_observation->stage = ActiveTouchStage::Constraint;
      flush_active_observation();
      runtime_sink.SetBallRetainer(nullptr);
      ball_retainer = nullptr;
    }
  }
  return ball_retainer;
}

Vector3 Player::GetBestPossibleTouch(football::sim::Tick now, const football::sim::PlayerTickContext& tick, const Vector3 &desiredTouch,
                                       e_FunctionType functionType) {
  constexpr float maxPowerShortPass = 30.0f;
  constexpr float maxPowerHighPass  = 42.0f;
  float maxPowerBase = maxPowerShortPass;
  if (functionType == e_FunctionType_HighPass) maxPowerBase = maxPowerHighPass;

  Vector3 resultTouch = desiredTouch;

  // fetch vars

  float maxPowerFactor = GetCurrentBakedClip().metadata.touch_max_power_factor;
  if (maxPowerFactor == 0.0f) maxPowerFactor = 1.0f;
  maxPowerFactor = maxPowerFactor * 0.7f + 0.3f;

  // clamp to maximum possible power (from anim vars)

  float maxPower = maxPowerBase * maxPowerFactor * (1.0f - clamp(decayingPositionOffset.GetLength() * 2.5f, 0.0f, 0.25f));
  maxPower += tick.ball.state().velocity.GetLength() * 0.5f; // can use some of current ballmomentum
  if (resultTouch.GetLength() > maxPower) {
    float missingPower = resultTouch.GetLength() - maxPower;
    resultTouch = resultTouch.GetNormalized(0) * maxPower;
    resultTouch.coords[2] += clamp(missingPower, 0.0f, 10.0f) * 0.25f;
  }

  // difficulty

  float difficultyFactor = GetCurrentBakedClip().metadata.touch_difficulty_factor;

  // apply stats
  if (functionType == e_FunctionType_ShortPass ||
      functionType == e_FunctionType_LongPass) difficultyFactor *= (1.0f - this->GetStat(football::player::PlayerStat::technical_shortpass) * 0.5f);
  if (functionType == e_FunctionType_HighPass) difficultyFactor *= (1.0f - this->GetStat(football::player::PlayerStat::technical_highpass)  * 0.5f);

  float distanceFactor = 0.0f;
  float heightFactor = 0.0f;
  float ballMovementFactor = 0.0f;
  GetDifficultyFactors(&tick.ball, this, tick.touches, tick.opponent_team, now, spatial_state_, decayingPositionOffset, distanceFactor, heightFactor, ballMovementFactor);

  // difficult balls may go into a less controlled orientation, or, if the anim
  // has a default outgoing direction, it may converge towards that (since it is
  // the easiest direction for that anim)
  radian randomRotation = 0.0f;
  randomRotation = distanceFactor * 0.15f + heightFactor * 0.15f + ballMovementFactor * 0.3f + difficultyFactor * 0.5f;
  Vector3 animBallDirection = GetCurrentBakedClip().metadata.outgoing_ball_direction.GetRotated2D(startAngle + anim_state_.rotationSmuggleOffset);
  if (animBallDirection.GetLength() > 0.01f) {
    float bias = clamp(randomRotation * 1.5f, 0.0f, 1.0f);
    Vector3 nativeTouch = animBallDirection.GetNormalized(resultTouch).Get2D() * resultTouch.GetLength() + resultTouch * Vector3(0, 0, 1);
    resultTouch = resultTouch * (1.0f - bias) + nativeTouch * bias;
  } else {
    // Deterministic deflection: the ball leaves to the side it already sits on
    // relative to the body, so the same contact geometry always repeats.
    const Vector3 toBall =
        tick.ball.state().position.Get2D() - spatial_state_.position;
    const float lateralSign =
        signSide(toBall.GetAngle2D(spatial_state_.directionVec));
    radian rotation =
        std::min((real) randomRotation, 0.5f) * lateralSign;
    resultTouch.Rotate2D(rotation);
  }

  // ball far away == less power
  resultTouch *= 1.0f - distanceFactor * 0.3f;
  resultTouch.coords[2] += distanceFactor * 1.5f; // try to correct (add power) by playing higher ball (== less ground friction)

  resultTouch.coords[2] += tick.ball.state().velocity.coords[2] * heightFactor * 0.5f +
                           heightFactor * 1.0f;

  resultTouch = resultTouch * (1.0f - ballMovementFactor) +
                tick.ball.state().velocity * ballMovementFactor;

  resultTouch.coords[2] += difficultyFactor * 5.0f;  // harder balls rise more

  return resultTouch;
}
