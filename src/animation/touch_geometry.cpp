// ANIM-1 A6.4 step 2b / ANIM-2 A12.3 — the stateless per-candidate touch
// geometry, moved verbatim out of touch_geometry.hpp. Same clamping, same
// operation order and same floating-point expressions.

#include "football/animation/touch_geometry.hpp"

#include <algorithm>
#include <cmath>

#include "football/animation/selection_math.hpp"

namespace football::animation {

float SegmentProjectionParameter(const Vector3& a, const Vector3& b,
                                 const Vector3& point) {
  if (a == b) return 0.0f;
  const float lineDistance = (b - a).GetLength();
  if (lineDistance < 0.000001f) return 0.0f;
  return ((point.coords[0] - a.coords[0]) * (b.coords[0] - a.coords[0]) +
          (point.coords[1] - a.coords[1]) * (b.coords[1] - a.coords[1])) /
         (lineDistance * lineDistance);
}

Vector3 FrontOfFootOffsetRel(float velocity, radian bodyAngleRel, float height,
                             float touch_offset_ms) {
  float fullDistanceFactor = 1.0f;
  float distance = 0.34f + velocity * touch_offset_ms * 0.001f * fullDistanceFactor;  // must be > 0 (for upcoming dotproduct)

  Vector3 ffo = Vector3(0, -distance * 0.8f, 0);

  radian bodyAngle = bodyAngleRel;
  if (velocity < idleDribbleSwitch) bodyAngle = 0;
  Vector3 angled = Vector3(0, -distance * 0.2f, 0).GetRotated2D(bodyAngle);

  return (ffo + angled) * (1.0f - clamp((height - 0.11f) / 4.0f, 0.0f, 0.5f));
}

TouchGeometryResult EvaluateTouchGeometry(const TouchGeometryRequest& request) {
  TouchGeometryResult result;

  Vector3 animBallPos = request.anim_ball_pos;
  Vector3 ballPos = request.ball_pos;
  Vector3 ballMovement = request.ball_movement;

  const float animBallHeight = animBallPos.coords[2];
  if (request.allow_pre_touch_rotation_smuggle) {
    animBallPos = (animBallPos - request.anim_body_pos)
                      .GetRotated2D(request.rotation_smuggle *
                                    (static_cast<float>(request.anim_touch_frame) /
                                     static_cast<float>(request.frame_count))) +
                  request.touch_body_pos_rotated;
  } else {
    animBallPos = (animBallPos - request.anim_body_pos) + request.touch_body_pos_rotated;
  }
  animBallPos.coords[2] = animBallHeight * request.height_scale;

  // now pick the ballPos from the previous 9ms that is closest to animBallPos.
  // this is to emulate a continuous 'close enough?'-check instead of a 'single
  // moment' check.
  if (request.use_continuous_ball_check) {
    const Vector3 ballLineA = ballPos - ballMovement * 0.006f;
    const Vector3 ballLineB = ballPos + ballMovement * 0.003f;
    float u = clamp(SegmentProjectionParameter(ballLineA, ballLineB, animBallPos), 0.0f, 1.0f);
    ballPos = ballLineA + (ballLineB - ballLineA) * u;
  }

  const Vector3 actionSmuggleVec3D = ballPos - animBallPos;
  const Vector3 actionSmuggleVec2D = actionSmuggleVec3D.Get2D();

  float ballDistanceZ = std::fabs(actionSmuggleVec3D.coords[2]);
  ballDistanceZ *= 1.0f - clamp((animBallPos.coords[2] - 0.11f) * 0.3f, 0.0f, 0.2f);  // higher balls == cheat more Z
  ballDistanceZ *= 1.0f - clamp((ballPos.coords[2] - 0.11f) * 0.4f, 0.0f, 0.3f);
  // enforce maximum height though
  if (ballPos.coords[2] > 1.8f && ballPos.coords[2] > animBallPos.coords[2] + 0.12f) ballDistanceZ *= 2.0f;
  if (ballPos.coords[2] > 2.6f && ballPos.coords[2] > animBallPos.coords[2] + 0.08f) ballDistanceZ *= 20.0f;
  if (request.function_type == e_FunctionType_Deflect) ballDistanceZ *= 0.8f;
  if (request.ball_retained_by_player) ballDistanceZ = 0.0f;
  if (ballPos.coords[2] < 0.5f && request.is_base) ballDistanceZ = std::max(ballDistanceZ - 0.15f, 0.0f);

  result.anim_ball_pos = animBallPos;
  result.ball_pos = ballPos;
  result.action_smuggle_vec2d = actionSmuggleVec2D;
  result.ball_distance_z = ballDistanceZ;

  if (ballDistanceZ < 0.22f) {
    float touchFrameAwkwardness =
        NormalizedClamp(std::abs(request.default_touch_frame - request.anim_touch_frame), 0.0f, 4.0f);
    touchFrameAwkwardness = std::pow(touchFrameAwkwardness, 2.0f) * 0.5f;

    float touchFrameFactor = request.anim_touch_frame / 24.0f;  // makes it 0.5 around the 'average touchframe'
    touchFrameFactor = std::pow(touchFrameFactor, 0.7f);
    // even steeper falloff after 1.0
    if (touchFrameFactor > 1.0f) touchFrameFactor = 1.0f + ((touchFrameFactor - 1.0f) * 0.5f);

    float radiusCheatOffset = 0.0f;
    float radiusFactor = 0.3f * (1.0f - touchFrameAwkwardness);

    if (request.function_type == e_FunctionType_Deflect) {
      radiusFactor *= 1.8f;
      radiusCheatOffset += 0.4f;
    }
    if (request.function_type == e_FunctionType_Sliding) {
      radiusFactor *= 0.2f;
      radiusCheatOffset = 0.0f;
    }  // prefer sliding without ball touch
    if (request.function_type == e_FunctionType_Interfere) {
      radiusFactor *= 1.4f;
      radiusCheatOffset += 0.2f;
    }
    if (request.function_type == e_FunctionType_ShortPass) {
      radiusFactor *= 1.3f;
      radiusCheatOffset += 0.15f;
    }
    if (request.function_type == e_FunctionType_LongPass) {
      radiusFactor *= 1.3f;
      radiusCheatOffset += 0.15f;
    }
    if (request.function_type == e_FunctionType_HighPass) {
      radiusFactor *= 1.3f;
      radiusCheatOffset += 0.15f;
    }
    if (request.function_type == e_FunctionType_Shot) {
      radiusFactor *= 1.3f;
      radiusCheatOffset += 0.15f;
    }

    if ((request.function_type == e_FunctionType_Trap ||
         request.function_type == e_FunctionType_BallControl) &&
        request.prefer_pass_and_shot == true) {
      radiusFactor *= 0.3f;
    }

    radiusCheatOffset *= (1.0f - touchFrameAwkwardness);

    float touchVelo = request.touch_movement.GetLength();

    Vector3 FFO = FrontOfFootOffsetRel(touchVelo, request.body_rotation_z,
                                       ballPos.coords[2], request.touch_offset_ms);
    if (FloatToEnumVelocity(touchVelo) == e_Velocity_Idle) {
      // always towards y = -1, right? (hmmm not really)
    } else {
      FFO.Rotate2D(FixAngle(request.touch_movement.GetNormalized(Vector3(0, -1, 0)).GetAngle2D()));
    }
    // just touched ball
    float lastTouchBias = curve(request.last_touch_bias, 1.0f);
    if (lastTouchBias > 0.0f) {
      float factor = 1.0f - lastTouchBias * 0.97f * (1.0f - request.technical_ballcontrol * 0.1f);
      radiusFactor *= factor;
      radiusCheatOffset *= factor;
    }

    result.within_reach = true;
    result.touch_framed_radius_factor = radiusFactor * touchFrameFactor;
    result.radius_cheat_offset = radiusCheatOffset;
    result.ffo = FFO;
  }

  return result;
}

}  // namespace football::animation
