// ANIM-2 A10.4a — the touch-match implementation.
//
// A verbatim relocation of the Player's touch-candidate loop. All Player,
// Ball, Pitch and MentalImage reads are replaced by the value context and the
// TouchMatchQueries port, in the exact legacy order and with the legacy time
// units. No RNG is drawn, no action is installed and no smuggle is applied.
#include "football/animation/touch_match.hpp"

#include <cassert>
#include <cmath>
#include <vector>

#include "football/animation/movement_math.hpp"
#include "football/animation/touch_geometry.hpp"
#include "football/animation/touch_scoring.hpp"

using namespace football::math;

namespace football::animation {

namespace {

// Tuning constants used only by the touch-selection loop.
constexpr float kCheatFactor = 0.5f;
constexpr float kCheatDistanceBonus = 0.02f;
constexpr float kCheatDiscardDistance = 0.02f;
constexpr float kCheatDiscardDistanceMultiplier = 0.4f;
constexpr float kMaxSmuggleDiscardDistance = 0.2f;
constexpr bool kEnableActionSmuggleDiscard = true;
constexpr bool kForceFullActionSmuggleDiscard = false;
constexpr bool kDiscardForwardSmuggle = true;
constexpr bool kDiscardSidewaysSmuggle = false;

// The Player's CalculateOutgoingMovement, over a borrowed reference motion.
Vector3 OutgoingMovementOf(std::span<const Vector3> positions) {
  if (positions.size() < 2) return Vector3(0);
  return (positions[positions.size() - 1] - positions[positions.size() - 2]) * 100.0f;
}

}  // namespace

TouchMatchResult MatchTouch(const AnimationLibrary& library,
                           const TouchMatchContext& context,
                           TouchMatchQueries& queries,
                           TouchMatchTrace* trace) {
  TouchMatchResult result;
  // ANIM-2 A11.7: a caller may reuse one trace across matches. Reset it here,
  // keeping the candidate vector's capacity, so an early return cannot leak the
  // previous match's diagnostics.
  if (trace != nullptr) {
    trace->candidates.clear();
    trace->result = TouchMatchResult{};
    trace->rejected_before_candidates = false;
    trace->early_reject = TouchRejectReason::None;
  }

  // Someone else holds the ball: no touch animation may be selected.
  if (context.ball_retained_by_other) {
    if (trace != nullptr) {
      trace->rejected_before_candidates = true;
      trace->early_reject = TouchRejectReason::RetainedByOther;
    }
    return result;
  }
  if (context.candidates.empty()) return result;

  const float angle = context.spatial_angle;
  const Vector3 incomingMovement = context.incoming_movement;
  const Vector3 desiredMovement = context.desired_movement;

  // The legacy loop reads the function type from the FIRST candidate and uses it
  // for every candidate and for the smuggle request.
  const AnimationClip& first_clip =
      library.Get(static_cast<uint32_t>(context.candidates.front()));
  const e_FunctionType functionType =
      StringToFunctionType(static_cast<e_DefString>(first_clip.metadata.action_type));

  if (trace != nullptr) trace->candidates.reserve(context.candidates.size());

  AnimationId bestAnimID = -1;
  Vector3 bestActionSmuggleVec2D;
  bool found = false;

  MotionPlanView plan;
  radian rotationSmuggle_ret_tmp = 0;
  radian predictedAngle = 0;
  Vector3 adaptedOutgoingMovement;
  std::vector<Vector3> bestPositions;

  for (const int candidate : context.candidates) {
    if (found) break;
    const AnimationId clipId = static_cast<AnimationId>(candidate);
    const AnimationClip& clip = library.Get(static_cast<uint32_t>(clipId));
    const bool isBase = clip.metadata.base_animation;

    TouchCandidateTrace candidate_trace;
    candidate_trace.clip = clipId;

    rotationSmuggle_ret_tmp = 0;
    if (!queries.MotionPlanFor(clipId, plan)) {
      candidate_trace.reject_reason = TouchRejectReason::NoPlan;
      if (trace != nullptr) trace->candidates.push_back(candidate_trace);
      continue;
    }
    rotationSmuggle_ret_tmp = plan.rotation_offset;

    // anim space!
    predictedAngle = clip.metadata.outgoing_angle + rotationSmuggle_ret_tmp;
    predictedAngle = ModulateIntoRange(-pi, pi, predictedAngle);

    const Vector3 outgoingMovement = OutgoingMovementOf(plan.positions).GetRotated2D(-angle);
    adaptedOutgoingMovement = outgoingMovement;

    const int frameCount = static_cast<int>(clip.frame_count) - 1;
    const int totalTouches = static_cast<int>(clip.touches.size());
    const int defaultTouchFrame = clip.metadata.touch_frame;
    assert(defaultTouchFrame >= 0 && defaultTouchFrame < frameCount);

    std::vector<int> touchIDs;
    touchIDs.reserve(totalTouches);
    // first the middle one down to the first
    for (int i = totalTouches / 2; i > -1; i--) touchIDs.push_back(i);
    // then the 1-after-middle one and upwards
    for (int i = totalTouches / 2 + 1; i < totalTouches; i++) touchIDs.push_back(i);

    for (int touchNum = 0; touchNum < totalTouches && !found; ++touchNum) {
      const int touchID = touchIDs[touchNum];
      Vector3 animBallPos = clip.touches[touchID].position;
      const int animTouchFrame = clip.touches[touchID].frame;
      candidate_trace.touch_frame = animTouchFrame;

      // out of bounds?
      if (!context.ball_retained_by_player) {
        const Vector3 absBallPos = queries.BallPredictAtTicks(animTouchFrame);
        if (std::fabs(absBallPos.coords[0]) >
                context.pitch_half_length + context.pitch_line_half_width + 0.11f ||
            std::fabs(absBallPos.coords[1]) >
                context.pitch_half_width + context.pitch_line_half_width + 0.11f) {
          candidate_trace.reject_reason = TouchRejectReason::OutOfBounds;
          continue;
        }
      }

      const Vector3 touchMovement =
          CalculateMovementAtFrame(plan.positions, animTouchFrame).GetRotated2D(-angle);
      const Vector3 animTouchMovement =
          CalculateMovementAtFrame(clip.root_positions, animTouchFrame);

      Vector3 ballPos = queries.BallPredictionAtMs(animTouchFrame * 10);
      Vector3 ballMovement =
          (queries.BallPredictionAtMs(animTouchFrame * 10 + 10) - ballPos) * 100.0f;
      ballPos = (ballPos - context.spatial_position).GetRotated2D(-angle);
      ballMovement = ballMovement.GetRotated2D(-angle);

      Vector3 bodyPos = plan.positions[animTouchFrame].GetRotated2D(-angle);
      bodyPos.coords[2] = 0;

      const auto& touchPose = clip.poses[animTouchFrame];
      const Quaternion animBodyRot = touchPose.orientations[BodyPart::player];
      const Vector3 animBodyPos = touchPose.positions[BodyPart::player];

      real x, y, z;
      animBodyRot.GetAngles(x, y, z);

      TouchGeometryRequest touch_request;
      touch_request.anim_ball_pos = animBallPos;
      touch_request.anim_body_pos = animBodyPos;
      touch_request.touch_body_pos_rotated = plan.positions[animTouchFrame].GetRotated2D(-angle);
      touch_request.ball_pos = ballPos;
      touch_request.ball_movement = ballMovement;
      touch_request.touch_movement = touchMovement;
      touch_request.anim_touch_frame = animTouchFrame;
      touch_request.frame_count = frameCount;
      touch_request.default_touch_frame = defaultTouchFrame;
      touch_request.function_type = functionType;
      touch_request.is_base = isBase;
      touch_request.ball_retained_by_player = context.ball_retained_by_player;
      touch_request.allow_pre_touch_rotation_smuggle =
          context.allow_pre_touch_rotation_smuggle;
      touch_request.use_continuous_ball_check = context.use_continuous_ball_check;
      touch_request.prefer_pass_and_shot = context.prefer_pass_and_shot;
      touch_request.rotation_smuggle = rotationSmuggle_ret_tmp;
      touch_request.body_rotation_z = z;
      touch_request.height_scale = context.height_scale;
      touch_request.last_touch_bias = queries.LastTouchBiasAtTick(animTouchFrame);
      touch_request.technical_ballcontrol = context.technical_ballcontrol;
      touch_request.touch_offset_ms = context.touch_offset_ms;
      const TouchGeometryResult touch_result = EvaluateTouchGeometry(touch_request);

      animBallPos = touch_result.anim_ball_pos;
      ballPos = touch_result.ball_pos;
      candidate_trace.ball_pos_local = ballPos;
      candidate_trace.touch_movement_local = touchMovement;

      if (touch_result.within_reach) {
        const Vector3& FFO = touch_result.ffo;
        const float radiusCheatOffset = touch_result.radius_cheat_offset;
        const float touchFramedRadiusFactor = touch_result.touch_framed_radius_factor;

        const float bodyBallDistanceAdvantage = EvaluateBodyBallDistanceAdvantage(
            static_cast<int>(clip.frame_count), animTouchMovement, touchMovement,
            incomingMovement, adaptedOutgoingMovement, predictedAngle, bodyPos, FFO,
            animBallPos.Get2D(), ballPos.Get2D(), touchFramedRadiusFactor,
            radiusCheatOffset, kCheatFactor, kCheatDistanceBonus);
        candidate_trace.body_ball_distance_advantage = bodyBallDistanceAdvantage;

        if (bodyBallDistanceAdvantage >= 1.0f || context.ball_retained_by_player) {
          found = true;
          bestAnimID = clipId;
          bestActionSmuggleVec2D = touch_result.action_smuggle_vec2d;
          bestPositions.assign(plan.positions.begin(), plan.positions.end());
          result.touch_frame = animTouchFrame;
          candidate_trace.reject_reason = TouchRejectReason::None;
        } else {
          candidate_trace.reject_reason = TouchRejectReason::AdvantageTooLow;
        }
      } else {
        candidate_trace.reject_reason = TouchRejectReason::OutOfReach;
      }
    }
    if (trace != nullptr) trace->candidates.push_back(candidate_trace);
  }

  if (found) {
    result.clip_id = bestAnimID;
    result.touch_position =
        queries.BallPredictionAtMs(result.touch_frame * 10);
    result.action_smuggle = bestActionSmuggleVec2D.GetRotated2D(angle);
    result.positions = std::move(bestPositions);

    ResolveActionSmuggleRequest smuggle_request;
    smuggle_request.raw_smuggle = result.action_smuggle;
    smuggle_request.touch_pos_z = result.touch_position.coords[2];
    smuggle_request.anim_touch_frame = result.touch_frame;
    smuggle_request.function_type = functionType;
    smuggle_request.ball_retained_by_player = context.ball_retained_by_player;
    smuggle_request.predicted_angle = predictedAngle;
    smuggle_request.spatial_angle = angle;
    smuggle_request.adapted_outgoing_movement = adaptedOutgoingMovement;
    smuggle_request.force_full_action_smuggle_discard = kForceFullActionSmuggleDiscard;
    smuggle_request.enable_action_smuggle_discard = kEnableActionSmuggleDiscard;
    smuggle_request.cheat_discard_distance = kCheatDiscardDistance;
    smuggle_request.cheat_discard_distance_multiplier = kCheatDiscardDistanceMultiplier;
    smuggle_request.max_smuggle_discard_distance = kMaxSmuggleDiscardDistance;
    smuggle_request.discard_forward_smuggle = kDiscardForwardSmuggle;
    smuggle_request.discard_sideways_smuggle = kDiscardSidewaysSmuggle;
    smuggle_request.closest_opponent_distance = context.closest_opponent_distance;
    result.action_smuggle = ResolveActionSmuggle(smuggle_request);
  }

  result.rotation_smuggle = rotationSmuggle_ret_tmp;
  if (trace != nullptr) trace->result = result;
  return result;
}

}  // namespace football::animation
