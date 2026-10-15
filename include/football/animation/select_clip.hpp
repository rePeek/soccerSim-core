#ifndef FOOTBALL_ANIMATION_SELECT_CLIP_HPP
#define FOOTBALL_ANIMATION_SELECT_CLIP_HPP

#include <cstdint>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/selector.hpp"
#include "football/animation/types.hpp"

// ANIM-1 A6.2 — stateless clip selection.
//
// Given an immutable clip list and a request, return the best-matching clip id.
// Pure: no RNG, no Player/Ball/Simulation, and it never mutates playback state.
// This is the Animation-side half of clip choice; the Player still decides WHEN
// and WHETHER to switch and owns the playback cursor/transition.

enum class ClipAction : std::uint8_t {
  Move = 0,
  Shoot,
  ShortPass,
  LongPass,
  HighPass,
  Dribble,
  Trap,
  Tackle,
  Save,
};

inline e_FunctionType ClipActionFunctionType(ClipAction action) {
  switch (action) {
    case ClipAction::Move: return e_FunctionType_Movement;
    case ClipAction::Shoot: return e_FunctionType_Shot;
    case ClipAction::ShortPass: return e_FunctionType_ShortPass;
    case ClipAction::LongPass: return e_FunctionType_LongPass;
    case ClipAction::HighPass: return e_FunctionType_HighPass;
    case ClipAction::Dribble: return e_FunctionType_BallControl;
    case ClipAction::Trap: return e_FunctionType_Trap;
    case ClipAction::Tackle: return e_FunctionType_Sliding;
    case ClipAction::Save: return e_FunctionType_Deflect;
  }
  return e_FunctionType_None;
}

struct SelectionRequest {
  ClipAction action = ClipAction::Move;
  // Movement bucket selection speed (m/s). Ignored for non-Movement actions.
  float desired_speed = 0.0f;
};

struct SelectionResult {
  AnimationId clip_id = -1;
  bool valid = false;
};

inline SelectionResult SelectClip(const std::vector<AnimationClip>& clips,
                                  const SelectionRequest& request) {
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = ClipActionFunctionType(request.action);
  if (request.action == ClipAction::Move) {
    query.byIncomingVelocity = true;
    query.incomingVelocity = FloatToEnumVelocity(request.desired_speed);
  }
  DataSet candidates;
  BakedAnimationSelector::CrudeSelection(clips, query, candidates);

  SelectionResult result;
  if (candidates.empty()) return result;
  // Deterministic pick independent of iteration/permutation.
  int best = candidates.front();
  for (const int id : candidates) {
    if (id < best) best = id;
  }
  result.clip_id = best;
  result.valid = true;
  return result;
}

#endif  // FOOTBALL_ANIMATION_SELECT_CLIP_HPP
