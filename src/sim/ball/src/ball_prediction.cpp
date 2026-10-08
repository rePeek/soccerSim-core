#include "ball_prediction.hpp"

#include <algorithm>

#include "ball_physics.hpp"

using namespace blunted;

namespace football::ball::detail {

void BallPredictionCache::Invalidate() {
  valid_predictions_ = 0;
}

void BallPredictionCache::Mirror() {
  for (auto& prediction : predictions_) {
    prediction.Mirror();
  }
}

void BallPredictionCache::Reset(const Vector3& focus_position) {
  for (std::size_t i = 0;
       i < football::sim::ball_timing::kPredictionHorizon.value; i++) {
    predictions_[i] = focus_position;
  }
  valid_predictions_ = 0;
}

void BallPredictionCache::Compute(
    const PhysicsState& initial, const football::ball::BallConfig& config,
    const football::model::Pitch& pitch,
    const football::ball::BallEnvironment& environment,
    PredictionResult& result) {
  using football::sim::TickSpan;

  Vector3 nextPos = initial.position;
  Quaternion nextOrientation = initial.orientation;
  Vector3 momentumPredict = initial.momentum;
  Quaternion rotationPredict_ms = initial.rotation_ms;

  predictions_[0] = nextPos;

  bool use_cache = false;

  const auto horizon = football::sim::ball_timing::kPredictionHorizon +
                       football::sim::ball_timing::kPredictionCache;
  for (TickSpan ahead{1}; ahead < horizon; ahead += TickSpan{1}) {
    // Originally the game recomputed the ball's prediction 300 steps into the
    // future. Keep the legacy shift-by-one cache: when the freshly computed
    // first sample equals the previous second sample, shift the remaining
    // samples down instead of re-integrating them.
    if (use_cache) {
      predictions_[ahead.value] = predictions_[ahead.value + 1];
      continue;
    }

    PhysicsState advanced = Advance(
        PhysicsState{nextPos, momentumPredict, rotationPredict_ms, nextOrientation},
        config, pitch, environment, /*first_step=*/ ahead == TickSpan{1});

    momentumPredict = advanced.momentum;
    nextPos = advanced.position;
    rotationPredict_ms = advanced.rotation_ms;
    nextOrientation = advanced.orientation;

    if (ahead == TickSpan{1}) {
      result.step_one = PhysicsState{nextPos, momentumPredict, rotationPredict_ms, nextOrientation};
      if (valid_predictions_ > 0 && predictions_[2] == nextPos) {
        valid_predictions_--;
        use_cache = true;
      } else {
        valid_predictions_ = football::sim::ball_timing::kPredictionCache.value;
      }
    }
    predictions_[ahead.value] = nextPos;
  }
}

Vector3 BallPredictionCache::Sample(football::sim::TickSpan horizon) const {
  const auto last = football::sim::ball_timing::kPredictionHorizon -
                    football::sim::TickSpan{1};
  return predictions_[std::min(horizon, last).value];
}

void BallPredictionCache::CopyTo(std::vector<Vector3>& target) const {
  target.resize(football::sim::ball_timing::kPredictionHorizon.value);
  for (std::size_t i = 0; i < target.size(); i++) {
    target[i] = predictions_[i];
  }
}

}  // namespace football::ball::detail