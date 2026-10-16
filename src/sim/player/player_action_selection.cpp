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
#include "football/animation/touch_match.hpp"
#include "football/ball/ball.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/player/player_diagnostics.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
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

// ANIM-2 A10.4d: a callback-backed TouchMatchQueries. The Player builds the
// callbacks inside SelectBestTouchAnim, so the shadow reads the same facts in
// the same order without Animation knowing about Player/Ball.
struct FunctionQueries final : football::animation::TouchMatchQueries {
  std::function<Vector3(int)> ball_prediction_ms;
  std::function<Vector3(int)> ball_predict_ticks;
  std::function<float(int)> last_touch_bias;
  std::function<bool(AnimationId, football::animation::MotionPlanView&)> motion_plan;
  std::vector<Vector3> plan_positions;

  Vector3 BallPredictionAtMs(int ms) override { return ball_prediction_ms(ms); }
  Vector3 BallPredictAtTicks(int ticks) override { return ball_predict_ticks(ticks); }
  float LastTouchBiasAtTick(int ticks) override { return last_touch_bias(ticks); }
  bool MotionPlanFor(AnimationId clip, football::animation::MotionPlanView& out) override {
    return motion_plan(clip, out);
  }
};


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

  // ANIM-2 A10.4c: Animation::MatchTouch is the only touch-selection path.
  // The Player only supplies the tick facts through the context and the query
  // port; it never runs a candidate loop of its own.
  const Vector3 incomingMovement =
      spatial_state_.movement.GetRotated2D(-spatial_state_.angle);
  const Vector3 desiredMovement = desiredDirection * desiredVelocityFloat;

  FunctionQueries queries;
  queries.ball_prediction_ms = [&](int ms) -> Vector3 {
    return football::sim::observation::SampleMentalImage(history, mentalImageTime)
        ->GetBallPrediction(ms, now, tick.ball);
  };
  queries.ball_predict_ticks = [&](int ticks) -> Vector3 {
    return tick.ball
        .Predict(football::sim::TickSpan{static_cast<std::uint64_t>(ticks)})
        .position;
  };
  queries.last_touch_bias = [&](int ticks) -> float {
    return GetLastTouchBias(
        600, now + football::sim::TickSpan{static_cast<std::uint64_t>(ticks)});
  };
  queries.motion_plan = [&](AnimationId clip,
                            football::animation::MotionPlanView& out) -> bool {
    radian rotation_offset_ret = 0;
    Vector3 physicsVector = CalculatePhysicsVector(
        now, static_cast<int>(clip), useDesiredMovement, desiredMovement,
        useDesiredBodyDirection, desiredBodyDirectionRel, queries.plan_positions,
        rotation_offset_ret);
    out.positions = queries.plan_positions;
    out.outgoing_velocity = physicsVector;
    out.rotation_offset = static_cast<float>(rotation_offset_ret);
    return true;
  };

  football::animation::TouchMatchContext context;
  context.candidates = sortedDataSet;
  context.spatial_angle = spatial_state_.angle;
  context.spatial_position = spatial_state_.position;
  context.incoming_movement = incomingMovement;
  context.desired_movement = desiredMovement;
  context.use_desired_movement = useDesiredMovement;
  context.use_desired_body_direction = useDesiredBodyDirection;
  context.desired_body_direction_rel = desiredBodyDirectionRel;
  context.height_scale = BodyProfile().height_scale;
  context.technical_ballcontrol =
      GetStat(football::player::PlayerStat::technical_ballcontrol);
  context.pitch_half_length = tick.pitch.half_length();
  context.pitch_half_width = tick.pitch.half_width();
  context.pitch_line_half_width = tick.pitch.line_half_width();
  context.ball_retained_by_player = (tick.ball_retainer == this);
  context.ball_retained_by_other =
      (tick.ball_retainer != 0 && tick.ball_retainer != this);
  context.allow_pre_touch_rotation_smuggle = allowPreTouchRotationSmuggle;
  context.use_continuous_ball_check = useContinuousBallCheck;
  context.prefer_pass_and_shot = preferPassAndShot;
  context.touch_offset_ms = static_cast<float>(defaultTouchOffset_ms);
  context.closest_opponent_distance =
      GetClosestOpponentDistance(tick.opponent_team);

  // ANIM-2 A11.6: production reads only the result, so the per-candidate trace
  // is not requested and no diagnostic vector is allocated.
  const football::animation::TouchMatchResult match =
      football::animation::MatchTouch(animations_, context, queries);

  choice.touch_frame = match.touch_frame;
  choice.touch_position = match.touch_position;
  choice.radius_offset = match.radius_offset;
  choice.action_smuggle = match.action_smuggle;
  choice.rotation_smuggle = match.rotation_smuggle;
  choice.positions = match.positions;
  return static_cast<int>(match.clip_id);
}
