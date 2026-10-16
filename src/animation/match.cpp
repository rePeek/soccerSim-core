// ANIM-2 A10.7: the Match entry point's definition.
//
// It lives in its own translation unit so the Animation module compiles to an
// independent archive and never links sim/player, sim/runtime, football/ball or
// sim/observation. The format and selection headers stay header-only and are
// still shared with the offline baker.
#include "football/animation/match.hpp"

namespace football::animation {

std::optional<MatchResult> Match(const AnimationLibrary& library,
                                 const MatchQuery& query) {
  std::vector<int> crude;
  if (!query.crude_candidates.empty()) {
    crude.assign(query.crude_candidates.begin(), query.crude_candidates.end());
  } else {
    DataSet selected;
    BakedAnimationSelector::CrudeSelection(library.Clips(), query.constraints.crude,
                                           selected);
    crude.assign(selected.begin(), selected.end());
  }
  if (crude.empty()) return std::nullopt;

  std::vector<int> candidates;
  std::vector<int> without_foot;
  switch (query.branch) {
    case MatchBranch::Movement:
      candidates = RefineMovementCandidates(
          library, crude, query.refinement,
          query.foot_counterfactual ? &without_foot : nullptr);
      break;
    case MatchBranch::NonMovement:
      candidates = RefineNonMovementCandidates(
          library, crude, query.refinement, query.constraints.strict,
          query.constraints.allowed_angle, query.constraints.allowed_velocity_steps,
          query.constraints.use_desired_trip_direction,
          query.constraints.desired_trip_direction);
      break;
    case MatchBranch::Legacy:
    default:
      candidates = RefineLegacyCandidates(
          library, crude, query.refinement, query.function_type,
          query.constraints.use_desired_trip_direction,
          query.constraints.desired_trip_direction,
          query.foot_counterfactual ? &without_foot : nullptr);
      break;
  }
  if (candidates.empty()) return std::nullopt;

  MatchResult result;
  result.clip_id = static_cast<AnimationId>(candidates.front());
  result.start_frame = 0;
  result.candidates = std::move(candidates);
  result.candidates_without_foot = std::move(without_foot);
  return result;
}

}  // namespace football::animation
