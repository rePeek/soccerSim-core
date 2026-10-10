#ifndef FOOTBALL_BALL_BALL_PREDICTION_HPP
#define FOOTBALL_BALL_BALL_PREDICTION_HPP

// Private to football::ball. Owns the legacy position-prediction array and its
// shift-by-one cache optimization. The cache is strictly read-only: it never
// feeds the authoritative BallState (see Ball::Step).

#include <span>
#include <vector>

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_environment.hpp"
#include "football/ball/ball_state.hpp"
#include "football/ball/ball_timing.hpp"
#include "football/ball/collider.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/ball_config.hpp"
#include "model/pitch.hpp"

namespace football::ball::detail {

class BallPredictionCache {
 public:
  BallPredictionCache() = default;

  void Invalidate();
  void Mirror();
  // Fills the legacy kPredictionHorizon samples, exactly like the old
  // ResetSituation body.
  void Reset(const blunted::Vector3& focus_position);

  // Recomputes the full horizon from `initial`. Pure read of `initial`; never
  // writes the authoritative state.
  void Compute(const BallState& initial,
               const football::model::BallConfig& config,
               const football::model::Pitch& pitch,
               std::span<const ColliderMotion> colliders,
               const BallDynamics& dynamics,
               const football::ball::BallEnvironment& environment);

  blunted::Vector3 Sample(football::sim::TickSpan horizon) const;
  void CopyTo(std::vector<blunted::Vector3>& target) const;

 private:
  blunted::Vector3 predictions_[football::sim::ball_timing::kPredictionHorizon.value +
                                football::sim::ball_timing::kPredictionCache.value + 1];
  int valid_predictions_ = 0;
};

}  // namespace football::ball::detail

#endif  // FOOTBALL_BALL_BALL_PREDICTION_HPP
