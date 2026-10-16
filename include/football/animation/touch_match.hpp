#ifndef FOOTBALL_ANIMATION_TOUCH_MATCH_HPP
#define FOOTBALL_ANIMATION_TOUCH_MATCH_HPP

#include <span>
#include <vector>

#include "football/animation/library.hpp"
#include "football/animation/match.hpp"

// ANIM-2 A10.4a — the touch-match context and query ports.
//
// The context carries candidate-independent values (the Player's pose and
// attributes, pitch bounds, ball retention and the legacy flags). The query
// port supplies candidate-dependent data on demand: the sampled ball
// prediction at a legacy millisecond offset, the raw ball prediction at a tick
// offset (the pitch-bounds check), the last-touch bias at a tick offset, and
// the adapted reference motion for a candidate clip.
//
// Animation never sees a Player, Ball, Pitch or Simulation; it only calls the
// ports. The legacy time units are kept verbatim for now:
//   GetBallPrediction(animTouchFrame * 10, now, ball)
//   GetLastTouchBias(600, now + animTouchFrame)
// They are intentionally not normalised in this step.

namespace football::animation {

// Candidate-independent inputs for one touch match.
struct TouchMatchContext {
  // The refined candidate order the caller already computed.
  std::span<const int> candidates;

  float spatial_angle = 0.0f;
  Vector3 spatial_position;
  // spatialState.movement rotated by -spatial_angle.
  Vector3 incoming_movement;
  // command.desiredDirection * command.desiredVelocityFloat.
  Vector3 desired_movement;
  bool use_desired_movement = false;
  bool use_desired_body_direction = false;
  Vector3 desired_body_direction_rel;

  float height_scale = 1.0f;
  float technical_ballcontrol = 0.0f;

  float pitch_half_length = 0.0f;
  float pitch_half_width = 0.0f;
  float pitch_line_half_width = 0.0f;

  bool ball_retained_by_player = false;
  // Somebody else holds the ball: no touch animation may be selected.
  bool ball_retained_by_other = false;

  bool allow_pre_touch_rotation_smuggle = false;
  bool use_continuous_ball_check = false;
  bool prefer_pass_and_shot = false;
  float touch_offset_ms = 0.0f;
  float closest_opponent_distance = 0.0f;
};

// Candidate-dependent, synchronous, order-preserving port. Calls must be made
// in the same order the legacy loop made them, so short-circuiting is
// preserved; do not batch-evaluate candidates up front.
class TouchMatchQueries {
 public:
  virtual ~TouchMatchQueries() = default;
  // Legacy `mentalImage->GetBallPrediction(ms, now, ball)`.
  virtual Vector3 BallPredictionAtMs(int ms) = 0;
  // Legacy `ball.Predict(TickSpan{ticks}).position`.
  virtual Vector3 BallPredictAtTicks(int ticks) = 0;
  // Legacy `GetLastTouchBias(600, now + TickSpan{ticks})`.
  virtual float LastTouchBiasAtTick(int ticks) = 0;
  // The adapted reference motion for one candidate. Returns false when no plan
  // exists. The returned view must stay valid until the next call.
  virtual bool MotionPlanFor(AnimationId clip_id, MotionPlanView& out) = 0;
};

enum class TouchRejectReason {
  None,
  RetainedByOther,
  OutOfBounds,
  NoPlan,
  OutOfReach,
  AdvantageTooLow,
};

// One candidate's outcome, enough to compare two implementations per candidate.
struct TouchCandidateTrace {
  AnimationId clip = -1;
  int touch_frame = -1;
  TouchRejectReason reject_reason = TouchRejectReason::None;
  float body_ball_distance_advantage = 0.0f;
  Vector3 ball_pos_local;        // ball position in animation space
  Vector3 touch_movement_local;  // touch movement in animation space
};

struct TouchMatchResult {
  AnimationId clip_id = -1;
  int touch_frame = -1;
  Vector3 touch_position;      // world space
  Vector3 action_smuggle;      // world space, z == 0
  radian rotation_smuggle = 0.0f;
  std::vector<Vector3> positions;  // winning candidate's adapted reference motion
  float radius_offset = 1000.0f;
};

struct TouchMatchTrace {
  std::vector<TouchCandidateTrace> candidates;
  TouchMatchResult result;
  // Set when the whole call is rejected before any candidate is evaluated.
  bool rejected_before_candidates = false;
  TouchRejectReason early_reject = TouchRejectReason::None;
};

// Run the touch match. Side-effect free: it never installs an action, applies a
// smuggle or draws RNG. The queries port is called in the legacy order.
TouchMatchTrace MatchTouch(const AnimationLibrary& library,
                           const TouchMatchContext& context,
                           TouchMatchQueries& queries);

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_TOUCH_MATCH_HPP
