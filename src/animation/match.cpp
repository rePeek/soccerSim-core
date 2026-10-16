// ANIM-2 A10.7: the Match entry point's definition.
//
// It lives in its own translation unit so the Animation module compiles to an
// independent archive and never links sim/player, sim/runtime, football/ball or
// sim/observation. The format and selection headers stay header-only and are
// still shared with the offline baker.
#include "football/animation/match.hpp"

#include <algorithm>

#include "football/animation/select_movement.hpp"

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

  // ANIM-2 A14: Trip has its own scoring (impact direction + velocity + pose
  // continuity). Put the winner in front so the Player installs
  // candidates.front().
  if (query.branch == MatchBranch::Trip) {
    const TripMatchResult trip = SelectTripMatch(library, crude, query.trip);
    if (!trip.matched) return std::nullopt;
    auto winner = std::find(crude.begin(), crude.end(), static_cast<int>(trip.clip_id));
    if (winner != crude.end() && winner != crude.begin())
      std::rotate(crude.begin(), winner, std::next(winner));
    MatchResult result;
    result.clip_id = trip.clip_id;
    result.start_frame = trip.start_frame;
    result.candidates = std::move(crude);
    return result;
  }

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

  // ANIM-2 A13.3: the final Movement pick rule is Animation's. Given the
  // locomotion preference, the preferred clip wins when it is a legal candidate;
  // otherwise the legacy front candidate plays. The reorder happens before the
  // entry-frame match, so the result's (clip_id, start_frame) is final and the
  // Player installs it without re-deciding.
  if (query.branch == MatchBranch::Movement && query.apply_movement_preference &&
      query.preferred_movement_clip >= 0) {
    const AnimationId pick =
        ResolveMovementPick(candidates, query.preferred_movement_clip);
    auto picked = std::find(candidates.begin(), candidates.end(),
                            static_cast<int>(pick));
    if (picked != candidates.end() && picked != candidates.begin())
      std::rotate(candidates.begin(), picked, std::next(picked));
  }

  MatchResult result;
  result.clip_id = static_cast<AnimationId>(candidates.front());
  result.start_frame = 0;
  // ANIM-2 A13: choose a legal entry frame whose pose best matches the actor's
  // current final pose, against the clip that will actually play.
  if (query.entry.enabled) {
    const AnimationClip& clip =
        library.Get(static_cast<uint32_t>(result.clip_id));
    EntryFrameRequest entry_request;
    entry_request.reference_pose = &query.entry.reference_pose;
    entry_request.min_remaining_frames = query.entry.min_remaining_frames;
    entry_request.last_legal_frame = query.entry.last_legal_frame;
    const EntryFrameResult entry = SelectEntryFrame(clip, entry_request);
    if (entry.matched) result.start_frame = entry.frame;
  }
  result.candidates = std::move(candidates);
  result.candidates_without_foot = std::move(without_foot);
  return result;
}

}  // namespace football::animation
