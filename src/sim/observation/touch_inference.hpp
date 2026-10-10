#ifndef FOOTBALL_SIM_OBSERVATION_TOUCH_INFERENCE_HPP
#define FOOTBALL_SIM_OBSERVATION_TOUCH_INFERENCE_HPP

#include <vector>

#include "model/player.hpp"
#include "sim/animation/library.hpp"
#include "sim/observation/snapshot.hpp"

namespace football::sim::observation {

// One touch inferred purely from consecutive snapshots. `confidence` is an
// ordering/diagnostic heuristic, not a calibrated probability.
struct TouchCandidate {
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  float confidence = 0.0f;
};

// Read-only, RNG-free touch inference between two consecutive snapshots.
//
// Takes SnapshotRecord rather than bare Snapshot because the caller-visible
// stamps are what allow refusing inference across generation changes,
// non-consecutive samples and topology changes. Returns an empty vector
// whenever the pair is not safely inferable. Candidates are ordered by
// confidence (descending), with roster slot order as a stable tie-break;
// several players in one step are all reported, never collapsed to one.
//
// This is deliberately a measurement instrument: snapshots record the final
// state after a step, so touches whose action clip already changed within the
// step are not recoverable. It does not touch TouchState, actors, the ball,
// the referee or the event recognizer.
std::vector<TouchCandidate> InferTouches(const SnapshotRecord& previous,
                                         const SnapshotRecord& current,
                                         const SnapshotMetadata& metadata,
                                         const AnimationLibrary& animations);

}  // namespace football::sim::observation

#endif  // FOOTBALL_SIM_OBSERVATION_TOUCH_INFERENCE_HPP
