#ifndef FOOTBALL_BALL_BALL_PREDICTION_HPP
#define FOOTBALL_BALL_BALL_PREDICTION_HPP

// Private to football::ball. Owns the legacy prediction array and its
// shift-by-one cache optimization. Consumers see predictions only through
// Ball::Predict / Ball::GetPredictionArray.

#include <vector>

#include "football/ball/ball_config.hpp"
#include "football/ball/ball_environment.hpp"
#include "football/ball/ball_timing.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"
#include "ball_physics.hpp"
#include "model/pitch.hpp"
#include "sim/time/tick.hpp"

namespace football::ball::detail {

// PhysicsState is defined by ball_physics.hpp; PredictionResult stores it by value.

struct PredictionResult {
  // Physical state advanced by the first prediction tick.
  PhysicsState step_one;
};

class BallPredictionCache {
 public:
  BallPredictionCache() = default;

  void Invalidate();
  void Mirror();
  // Fills only the legacy kPredictionHorizon samples, exactly like the old
  // ResetSituation body.
  void Reset(const blunted::Vector3& focus_position);

  // Recomputes the full horizon from `initial` and returns the step-one state.
  void Compute(const PhysicsState& initial,
               const football::ball::BallConfig& config,
               const football::model::Pitch& pitch,
               const football::ball::BallEnvironment& environment,
               PredictionResult& result);

  blunted::Vector3 Sample(football::sim::TickSpan horizon) const;
  void CopyTo(std::vector<blunted::Vector3>& target) const;

 private:
  blunted::Vector3 predictions_[football::sim::ball_timing::kPredictionHorizon.value +
                                football::sim::ball_timing::kPredictionCache.value + 1];
  int valid_predictions_ = 0;
};

}  // namespace football::ball::detail

#endif  // FOOTBALL_BALL_BALL_PREDICTION_HPP
