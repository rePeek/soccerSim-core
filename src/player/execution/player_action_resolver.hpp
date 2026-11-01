#ifndef PLAYER_ACTION_RESOLVER_HPP
#define PLAYER_ACTION_RESOLVER_HPP

#include <optional>
#include <span>
#include <string>
#include <vector>


#include "football/animation/entry_frame.hpp"
#include "football/animation/library.hpp"
#include "player/execution/player_action_candidates.hpp"
#include "player/execution/player_crude_query.hpp"
#include "player/execution/player_crude_filter.hpp"
#include "player/execution/player_reaction_resolver.hpp"

// The Player's action resolver: which clip performs this action, and from which
// frame.
//
// ACFG-3 moved this decision out of the Animation module. Animation is now the
// shared clip resource plus pure pose/clip algorithms; deciding what to perform
// is Player business, and the resolver is where that lives.
//
// The Player resolves its own state (command, spatial facts, precomputed ball
// sampling) into a `MatchQuery` and receives the chosen clip and cursor as a
// `MatchResult`. Motion adaptation is a synchronous, side-effect-free port: the
// Player implements it over PlayerMotion, so this resolver never reaches into
// the entity.

namespace football::player {

using namespace football::animation;

// A read-only view of one candidate's adapted reference motion, in the shape
// Animation's touch/pose matching understands. It borrows storage owned by the
// evaluator for the duration of the Match call.
// The final Movement pick: a preferred (motion-driven) clip is honoured only
// when it is already a legal candidate, otherwise the legacy first candidate
// plays. This mirrors the shipped rule exactly, with no ordering change.
inline AnimationId ResolveMovementPick(
    std::span<const int> legacy_ordered_candidates, AnimationId preferred) {
  if (legacy_ordered_candidates.empty()) return preferred;
  for (const int id : legacy_ordered_candidates) {
    if (id == static_cast<int>(preferred)) return preferred;
  }
  return static_cast<AnimationId>(legacy_ordered_candidates.front());
}

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
  // ANIM-2 A14: Trip candidates scored by impact direction/velocity/pose.
  Trip,
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
  // ANIM-2 A13.3: the FINAL selected clip. For Movement this already includes
  // the locomotion preference reorder, so the Player installs it directly.
  AnimationId clip_id = -1;
  // ANIM-2 A13: the chosen entry frame, matched against `clip_id`. 0 unless the
  // caller enabled pose-based entry-frame matching for this branch.
  int start_frame = 0;
  std::optional<TouchMatch> touch;
};

// ANIM-2 A13.0: pose-based entry-frame matching. `Match` applies it to the
// final selected clip after any Movement preference reorder. The reference pose
// is the actor's current final pose.
struct MatchEntryRequest {
  bool enabled = false;
  PoseFrame reference_pose;
  int min_remaining_frames = 1;
  int last_legal_frame = kEntryFrameUnbounded;
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
  // ANIM-2 A13.3: the locomotion strategy's preferred Movement clip, already
  // resolved by the Player. When `apply_movement_preference` is set and the clip
  // is a legal candidate, Match reorders the Movement candidates so it wins;
  // otherwise the legacy front candidate plays. Either way the result's
  // clip_id/start_frame are final.
  AnimationId preferred_movement_clip = -1;
  bool apply_movement_preference = false;
  // ANIM-2 A13: entry-frame matching for Movement/Trip.
  MatchEntryRequest entry;
  // ANIM-2 A14: the Trip inputs (used only by MatchBranch::Trip).
  TripMatchRequest trip;
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

}  // namespace football::player

#endif  // PLAYER_ACTION_RESOLVER_HPP
