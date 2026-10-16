#ifndef FOOTBALL_ANIMATION_MATCH_HPP
#define FOOTBALL_ANIMATION_MATCH_HPP

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "football/animation/entry_frame.hpp"
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

// ANIM-2 A10.5: which refinement the match runs. The Player decides the branch
// (it depends on the command and the crude set); Match only dispatches.
enum class MatchBranch {
  // `useDesiredMovement == false`: priority/idle/foot/incoming/trip/baseanim.
  Legacy,
  // `useDesiredMovement == true`, Movement: the Movement sort chain.
  Movement,
  // `useDesiredMovement == true`, non-Movement: the non-Movement chain.
  NonMovement,
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
  // The same chain minus the foot sort, when the caller asked for the foot
  // counterfactual diagnostic. Empty otherwise.
  std::vector<int> candidates_without_foot;
  // The selected clip and its entry cursor. For Movement/non-Movement this is
  // `candidates.front()`; for touch actions the A10.4 loop fills it.
  AnimationId clip_id = -1;
  // ANIM-2 A13: the chosen entry frame. 0 unless the caller enabled
  // pose-based entry-frame matching for this branch.
  int start_frame = 0;
  std::optional<TouchMatch> touch;
};

// ANIM-2 A13.0: pose-based entry-frame matching, applied to the front
// candidate. The reference pose is the actor's current final pose. The Player
// recomputes for its final clip after any locomotion reorder.
struct MatchEntryRequest {
  bool enabled = false;
  PoseFrame reference_pose;
  int min_remaining_frames = 1;
  int last_legal_frame = -1;
};

struct MatchQuery {
  e_FunctionType function_type = e_FunctionType_None;
  MatchConstraints constraints;
  // The Movement/non-Movement sort request (already resolved by the Player).
  MovementRefinementRequest refinement;
  // ANIM-2 A10.5: which refinement to run, and the crude candidate set the
  // Player already built (empty means Match runs CrudeSelection itself).
  MatchBranch branch = MatchBranch::Legacy;
  std::span<const int> crude_candidates;
  bool foot_counterfactual = false;
  // ANIM-2 A13: entry-frame matching for Movement/Trip.
  MatchEntryRequest entry;
};

// Refine the candidate set for the query and return the match. The Player
// decides the branch and supplies the crude set; Animation runs the stateless
// refinement and returns the ordered candidates plus the foot-counterfactual
// list. The touch stage has its own entry point (MatchTouch).
//
// ANIM-2 A10.7: the definition lives in src/animation/match.cpp, so the
// Animation module compiles to its own archive and never links sim/player.
std::optional<MatchResult> Match(const AnimationLibrary& library,
                                const MatchQuery& query);

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_MATCH_HPP
