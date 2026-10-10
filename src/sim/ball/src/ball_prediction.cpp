#include "ball_contact.hpp"
#include "ball_prediction.hpp"

#include <algorithm>

#include "football/ball/ball_contact.hpp"

using namespace blunted;

namespace football::ball::detail {

void BallPredictionCache::Invalidate() { valid_predictions_ = 0; }

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
    const BallState& initial, const football::model::BallConfig& config,
    const football::model::Pitch& pitch,
    std::span<const ColliderMotion> colliders, const BallDynamics& dynamics,
    const football::ball::BallEnvironment& environment) {
  using football::sim::TickSpan;

  BallState current = initial;
  predictions_[0] = current.position;

  const auto horizon = football::sim::ball_timing::kPredictionHorizon +
                       football::sim::ball_timing::kPredictionCache;
  for (TickSpan ahead{1}; ahead < horizon; ahead += TickSpan{1}) {
    BallStepResult result = AdvanceBallTick(
        current, colliders, football::sim::kTickSeconds, config, dynamics);
    ResolveNetting(result.state.position, result.state.velocity, pitch, config,
                   environment);
    current = result.state;
    predictions_[ahead.value] = current.position;
  }
  valid_predictions_ = football::sim::ball_timing::kPredictionCache.value;
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
