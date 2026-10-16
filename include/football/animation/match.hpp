#ifndef FOOTBALL_ANIMATION_MATCH_HPP
#define FOOTBALL_ANIMATION_MATCH_HPP

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "football/animation/library.hpp"
#include "football/animation/movement_selection.hpp"
#include "football/animation/selector.hpp"

// ANIM-2 A10.2 — the Animation-side match contract.
//
// This is the value/data boundary between the Player and the Animation module.
// The Player resolves its own state (command, spatial facts, precomputed ball
// sampling) into a `MatchQuery`; Animation returns the chosen clip and cursor
// as a `MatchResult`. Animation never includes sim/player, sim/runtime,
// football/ball or sim/observation: anything it needs arrives as a value.
//
// Motion adaptation is a synchronous, side-effect-free port. Animation asks
// `MotionEvaluator` for a candidate's adapted reference trajectory; the Player
// implements it over PlayerMotion. Animation never sees Player or PlayerMotion.
//
// A10.2 defines the contract and the stateless candidate refinement it already
// owns. The touch-matching loop (A10.4) and the Player wiring (A10.5) extend the
// same query/result rather than introducing a second selection path.

namespace football::animation {

// A read-only view of one candidate's adapted reference motion, in the shape
// Animation's touch/pose matching understands. It borrows storage owned by the
// evaluator for the duration of the Match call.
struct MotionPlanView {
  std::span<const Vector3> positions;
  Vector3 outgoing_velocity;
  float rotation_offset = 0.0f;
};

// Synchronous candidate-motion port. `Evaluate` must have no side effects and
// must not commit a position or advance the action clock; it may use a
// per-Match candidate cache. The returned view is only valid while the same
// Match call is in progress.
class MotionEvaluator {
 public:
  virtual ~MotionEvaluator() = default;
  virtual bool Evaluate(AnimationId clip_id, MotionPlanView& out) = 0;
};

// Resolved desired movement / look-at for this tick.
struct MatchMotionIntent {
  bool use_desired_movement = false;
  Vector3 desired_direction;
  float desired_velocity_float = 0.0f;
  bool use_desired_look_at = false;
  Vector3 desired_look_at;
};

// The Player's animation-relevant pose features. These mirror the fields the
// legacy selection read from the spatial state; they are values, not a Player.
struct MatchPose {
  Vector3 position;
  radian angle = 0.0f;
  Vector3 direction_vec{0, -1, 0};
  Vector3 body_direction_vec{0, -1, 0};
  Vector3 rel_body_direction_vec{0, -1, 0};
  Vector3 rel_body_direction_vec_nonquantized{0, -1, 0};
  radian rel_body_angle_nonquantized = 0.0f;
  e_Velocity enum_velocity = e_Velocity_Idle;
  float float_velocity = 0.0f;
  e_Foot foot = e_Foot_Right;
};

// The current animation, for the touch/pose matching stage (A10.4).
struct MatchAnimationState {
  AnimationId clip_id = -1;
  int frame = 0;
  e_FunctionType function_type = e_FunctionType_None;
  std::string outgoing_special_state;
  std::string outgoing_retain_state;
  int touch_frame = -1;
  Vector3 touch_position;
  std::vector<Vector3> positions;
};

// Candidate-set constraints the Player already resolved.
struct MatchConstraints {
  // The legacy crude query, built by the Player from its own state.
  CrudeSelectionQuery crude;
  // Non-Movement filter strictness.
  bool strict = false;
  radian allowed_angle = 0.0f;
  int allowed_velocity_steps = 0;
  bool use_desired_trip_direction = false;
  Vector3 desired_trip_direction;
  bool is_ball_control = false;
  // Idle level for the Movement branch (0 normally, 1 set-piece, 2 not playing).
  float idle_level = 0.0f;
};

// The matched touch of one candidate (filled by the A10.4 loop).
struct TouchMatch {
  int touch_frame = -1;
  Vector3 touch_position;
  Vector3 action_smuggle;
  radian rotation_smuggle = 0.0f;
};

struct MatchResult {
  // The ordered candidate list after refinement, in the exact legacy order.
  std::vector<int> candidates;
  // The selected clip and its entry cursor. For Movement/non-Movement this is
  // `candidates.front()`; for touch actions the A10.4 loop fills it.
  AnimationId clip_id = -1;
  int start_frame = 0;
  std::optional<TouchMatch> touch;
};

struct MatchQuery {
  e_FunctionType function_type = e_FunctionType_None;
  MatchMotionIntent motion;
  MatchPose pose;
  MatchAnimationState animation;
  MatchConstraints constraints;
  // The Movement/non-Movement sort request (already resolved by the Player).
  MovementRefinementRequest refinement;
};

// Refine the candidate set for the query and return the match. `motion` is used
// by the touch/pose stage; the Movement and non-Movement stages do not need it
// yet, so it may be an evaluator that is never called on those paths.
//
// A10.2 implements the stateless candidate refinement (crude selection plus the
// Movement / non-Movement stable-sort chain) and returns `candidates.front()`.
// The touch loop that consumes `motion` lands in A10.4; until then a touch
// action gets the same refined candidates the Player already used.
inline std::optional<MatchResult> Match(const AnimationLibrary& library,
                                        const MatchQuery& query,
                                        MotionEvaluator& motion) {
  (void)motion;
  DataSet crude;
  BakedAnimationSelector::CrudeSelection(library.Clips(), query.constraints.crude, crude);
  if (crude.empty()) return std::nullopt;

  std::vector<int> candidates;
  if (query.function_type == e_FunctionType_Movement) {
    candidates = RefineMovementCandidates(library, crude, query.refinement);
  } else {
    candidates = RefineNonMovementCandidates(
        library, crude, query.refinement, query.constraints.strict,
        query.constraints.allowed_angle, query.constraints.allowed_velocity_steps,
        query.constraints.use_desired_trip_direction,
        query.constraints.desired_trip_direction);
  }
  if (candidates.empty()) return std::nullopt;

  MatchResult result;
  result.clip_id = static_cast<AnimationId>(candidates.front());
  result.start_frame = 0;
  result.candidates = std::move(candidates);
  return result;
}

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_MATCH_HPP
