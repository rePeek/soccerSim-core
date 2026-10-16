// ANIM-2 A10.7: the Match entry point's definition.
//
// It lives in its own translation unit so the Animation module compiles to an
// independent archive and never links sim/player, sim/runtime, football/ball or
// sim/observation. The format and selection headers stay header-only and are
// still shared with the offline baker.
#include "football/animation/match.hpp"

namespace football::animation {

std::optional<MatchResult> Match(const AnimationLibrary& library,
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
