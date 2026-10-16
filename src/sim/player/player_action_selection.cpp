// ANIM-1 A6.4 step 9c: the touch-animation selection loop is Player-owned. This
// translation unit holds the relocated `GetBestCheatableAnimID` loop; the
// The compatibility state supplies the not-yet-migrated spatial facts and the pure
// reference-motion kernel (A8.0). No candidate, touch order or early exit was
// changed.

#include "sim/player/player.hpp"

#include "football/animation/movement_math.hpp"
#include "football/animation/selector.hpp"
#include "football/animation/touch_geometry.hpp"
#include "football/animation/touch_scoring.hpp"
#include "football/ball/ball.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player_motion_constants.hpp"

#include <cassert>
#include <cmath>

using namespace football::math;
using football::animation::CalculateMovementAtFrame;

namespace {

// Relocated tuning constants that are used only by the touch-selection loop.
constexpr float kCheatFactor = 0.5f;
constexpr float kCheatDistanceBonus = 0.02f;
constexpr float kCheatDiscardDistance = 0.02f;
constexpr float kCheatDiscardDistanceMultiplier = 0.4f;
constexpr float kMaxSmuggleDiscardDistance = 0.2f;
constexpr bool kEnableActionSmuggleDiscard = true;
constexpr bool kForceFullActionSmuggleDiscard = false;
constexpr bool kDiscardForwardSmuggle = true;
constexpr bool kDiscardSidewaysSmuggle = false;

}  // namespace

int Player::SelectBestTouchAnim(
    football::sim::Tick now, const football::sim::PlayerTickContext &tick,
    std::span<MentalImage> history, const DataSet &sortedDataSet,
    bool useDesiredMovement, const Vector3 &desiredDirection,
    float desiredVelocityFloat, bool useDesiredBodyDirection,
    const Vector3 &desiredBodyDirectionRel, football::sim::TouchChoice &choice,
    e_InterruptAnim localInterruptAnim, bool preferPassAndShot,
    bool allowPreTouchRotationSmuggle, bool useContinuousBallCheck) {
  (void)localInterruptAnim;

  // never allow touchanims when someone else is holding the ball in his/her hands
  if (tick.ball_retainer != 0 && tick.ball_retainer != this) return -1;

  Vector3 incomingMovement =
      spatial_state_.movement.GetRotated2D(-spatial_state_.angle);

  int bestAnimID = -1;
  Vector3 bestActionSmuggleVec2D;
  Vector3 bestTouchMovementAbs;

  DataSet::const_iterator iter = sortedDataSet.begin();

  Vector3 desiredMovement = desiredDirection * desiredVelocityFloat;

  e_FunctionType functionType = StringToFunctionType(
      static_cast<e_DefString>(GetBakedClip(*iter).metadata.action_type));

  radian rotationSmuggle_ret_tmp = 0;
  radian predictedAngle = 0;
  Vector3 adaptedOutgoingMovement;

  Vector3 dud;

  bool found = false;
  while (iter != sortedDataSet.end() && found == false) {
    const AnimationClip &clip = GetBakedClip(*iter);
    bool isBase = clip.metadata.base_animation;

    const std::vector<Vector3> &origPositionCache = clip.root_positions;

    Vector3 physicsVector = CalculatePhysicsVector(
        now, *iter, useDesiredMovement, desiredMovement,
        useDesiredBodyDirection, desiredBodyDirectionRel, choice.positions,
        rotationSmuggle_ret_tmp);

    // anim space!
    predictedAngle = clip.metadata.outgoing_angle + rotationSmuggle_ret_tmp;
    predictedAngle = ModulateIntoRange(-pi, pi, predictedAngle);

    // iterate all possible touches of this anim
    int touchNum = 0;
    Vector3 animBallPos;
    int animTouchFrame = 0;

    Quaternion animBodyRot;
    Vector3 animBodyPos;
    Vector3 bodyPos;
    Vector3 prevBodyPos;

    Vector3 outgoingMovement =
        CalculateOutgoingMovement(choice.positions)
            .GetRotated2D(-spatial_state_.angle);
    adaptedOutgoingMovement = outgoingMovement;  // may be changed into touchMovement later on, when an anim is found

    int frameCount = static_cast<int>(clip.frame_count) - 1;

    const int totalTouches = static_cast<int>(clip.touches.size());
    int touchIDs[totalTouches];
    int count = 0;

    int defaultTouchFrame = clip.metadata.touch_frame;
    assert(defaultTouchFrame >= 0 && defaultTouchFrame < frameCount);

    // first the middle one down to the first
    for (int i = totalTouches / 2; i > -1; i--) {
      touchIDs[count] = i;
      count++;
    }
    // then the 1-after-middle one and upwards
    for (int i = totalTouches / 2 + 1; i < totalTouches; i++) {
      touchIDs[count] = i;
      count++;
    }

    while (touchNum < totalTouches && found == false) {
      animBallPos = clip.touches[touchIDs[touchNum]].position;
      animTouchFrame = clip.touches[touchIDs[touchNum]].frame;

      // out of bounds?
      if (tick.ball_retainer != this) {
        Vector3 absBallPos = tick.ball.Predict(football::sim::TickSpan{static_cast<std::uint64_t>(animTouchFrame)}).position;
        if (std::fabs(absBallPos.coords[0]) > tick.pitch.half_length() + tick.pitch.line_half_width() + 0.11f ||
            std::fabs(absBallPos.coords[1]) > tick.pitch.half_width() + tick.pitch.line_half_width() + 0.11f) {
          touchNum++;
          continue;
        }
      }

      Vector3 touchMovement =
          CalculateMovementAtFrame(choice.positions, animTouchFrame)
              .GetRotated2D(-spatial_state_.angle);
      Vector3 animTouchMovement = CalculateMovementAtFrame(origPositionCache, animTouchFrame);  // already anim space so no: .GetRotated2D(-spatialState.angle);

      Vector3 ballPos, ballMovement;
      auto mentalImage = football::sim::observation::SampleMentalImage(
          history, mentalImageTime);
      ballPos = mentalImage->GetBallPrediction(animTouchFrame * 10, now, tick.ball);
      ballMovement = (mentalImage->GetBallPrediction(animTouchFrame * 10 + 10, now, tick.ball) - mentalImage->GetBallPrediction(animTouchFrame * 10, now, tick.ball)) * 100.0f;
      ballPos = (ballPos - spatial_state_.position)
                    .GetRotated2D(-spatial_state_.angle);
      ballMovement = ballMovement.GetRotated2D(-spatial_state_.angle);

      bodyPos = choice.positions.at(animTouchFrame)
                    .GetRotated2D(-spatial_state_.angle);
      bodyPos.coords[2] = 0;

      const auto &touchPose = clip.poses[animTouchFrame];
      animBodyRot = touchPose.orientations[BodyPart::player];
      animBodyPos = touchPose.positions[BodyPart::player];

      real x, y, z;
      animBodyRot.GetAngles(x, y, z);

      football::animation::TouchGeometryRequest touch_request;
      touch_request.anim_ball_pos = animBallPos;
      touch_request.anim_body_pos = animBodyPos;
      touch_request.touch_body_pos_rotated =
          choice.positions.at(animTouchFrame)
              .GetRotated2D(-spatial_state_.angle);
      touch_request.ball_pos = ballPos;
      touch_request.ball_movement = ballMovement;
      touch_request.touch_movement = touchMovement;
      touch_request.anim_touch_frame = animTouchFrame;
      touch_request.frame_count = frameCount;
      touch_request.default_touch_frame = defaultTouchFrame;
      touch_request.function_type = functionType;
      touch_request.is_base = isBase;
      touch_request.ball_retained_by_player = (tick.ball_retainer == this);
      touch_request.allow_pre_touch_rotation_smuggle = allowPreTouchRotationSmuggle;
      touch_request.use_continuous_ball_check = useContinuousBallCheck;
      touch_request.prefer_pass_and_shot = preferPassAndShot;
      touch_request.rotation_smuggle = rotationSmuggle_ret_tmp;
      touch_request.body_rotation_z = z;
      touch_request.height_scale = BodyProfile().height_scale;
      touch_request.last_touch_bias = GetLastTouchBias(
          600, now + football::sim::TickSpan{static_cast<std::uint64_t>(animTouchFrame)});
      touch_request.technical_ballcontrol =
          GetStat(football::player::PlayerStat::technical_ballcontrol);
      touch_request.touch_offset_ms = static_cast<float>(defaultTouchOffset_ms);
      const football::animation::TouchGeometryResult touch_result =
          football::animation::EvaluateTouchGeometry(touch_request);

      animBallPos = touch_result.anim_ball_pos;
      ballPos = touch_result.ball_pos;
      const Vector3 &actionSmuggleVec2D = touch_result.action_smuggle_vec2d;
      const float ballDistanceZ = touch_result.ball_distance_z;
      (void)ballDistanceZ;

      if (touch_result.within_reach) {
        const Vector3 &FFO = touch_result.ffo;
        const float radiusCheatOffset = touch_result.radius_cheat_offset;
        const float touchFramedRadiusFactor = touch_result.touch_framed_radius_factor;

        float bodyBallDistanceAdvantage =
            football::animation::EvaluateBodyBallDistanceAdvantage(
                static_cast<int>(clip.frame_count), animTouchMovement,
                touchMovement, incomingMovement, adaptedOutgoingMovement,
                predictedAngle, bodyPos, FFO, animBallPos.Get2D(), ballPos.Get2D(),
                touchFramedRadiusFactor, radiusCheatOffset, kCheatFactor,
                kCheatDistanceBonus);

        if (bodyBallDistanceAdvantage >= 1.0f ||
            tick.ball_retainer == this) {
          found = true;

          bestAnimID = static_cast<int>(*iter);
          bestActionSmuggleVec2D = actionSmuggleVec2D;
          bestTouchMovementAbs =
              touchMovement.GetRotated2D(spatial_state_.angle);
          choice.touch_frame = animTouchFrame;
          choice.radius_offset = 1000.0f;  // radiusOffset; todo? is this still in use?
        }
      }
      touchNum++;
    }
    iter++;
  }

  if (found) {
    auto currentMentalImage = football::sim::observation::SampleMentalImage(
        history, mentalImageTime);
    choice.touch_position = currentMentalImage->GetBallPrediction(
        choice.touch_frame * 10, now, tick.ball);
    choice.full_action_smuggle =
        bestActionSmuggleVec2D.GetRotated2D(spatial_state_.angle);

    football::animation::ResolveActionSmuggleRequest smuggle_request;
    smuggle_request.raw_smuggle = choice.full_action_smuggle;
    smuggle_request.touch_pos_z = choice.touch_position.coords[2];
    smuggle_request.anim_touch_frame = choice.touch_frame;
    smuggle_request.function_type = functionType;
    smuggle_request.ball_retained_by_player = (tick.ball_retainer == this);
    smuggle_request.predicted_angle = predictedAngle;
    smuggle_request.spatial_angle = spatial_state_.angle;
    smuggle_request.adapted_outgoing_movement = adaptedOutgoingMovement;
    smuggle_request.force_full_action_smuggle_discard = kForceFullActionSmuggleDiscard;
    smuggle_request.enable_action_smuggle_discard = kEnableActionSmuggleDiscard;
    smuggle_request.cheat_discard_distance = kCheatDiscardDistance;
    smuggle_request.cheat_discard_distance_multiplier = kCheatDiscardDistanceMultiplier;
    smuggle_request.max_smuggle_discard_distance = kMaxSmuggleDiscardDistance;
    smuggle_request.discard_forward_smuggle = kDiscardForwardSmuggle;
    smuggle_request.discard_sideways_smuggle = kDiscardSidewaysSmuggle;
    smuggle_request.closest_opponent_distance =
        GetClosestOpponentDistance(tick.opponent_team);
    choice.action_smuggle =
        football::animation::ResolveActionSmuggle(smuggle_request);

    assert(choice.action_smuggle.coords[2] == 0.0f);
    assert(choice.action_smuggle.coords[2] == 0.0f);
  }

  choice.rotation_smuggle = rotationSmuggle_ret_tmp;

  return bestAnimID;
}
