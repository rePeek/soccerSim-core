#pragma once

#include <optional>
#include <span>
#include <vector>

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_model.hpp"
#include "football/ball/ball_state.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::pitch { class Pitch; }

namespace football::ball {

struct BallTickInput {
  std::span<const football::geometry::ColliderMotion> dynamic_colliders;
  std::optional<BallImpulse> active_impulse;
};

class Ball {
 public:
  Ball(const football::ball::BallModel& model,
       const football::pitch::Pitch& pitch);
  ~Ball();

  BallState state() const;
  const football::ball::BallModel& config() const { return config_; }

  BallStepResult Step(const BallTickInput& input);
  BallState Predict(football::sim::TickSpan ahead) const;
  BallStepResult Predict(const BallTickInput& input) const;

  void Reset(const BallState& state);

  // Authoritative ground placement: z is forced to the ball radius and
  // velocity/spin are cleared. Airborne states still use Reset(BallState).
  void PlaceOnGround(float x, float y);

 private:
  BallStepResult AdvanceState(
      const BallState& current,
      std::span<const football::geometry::ColliderMotion> colliders) const;
  BallStepResult EvaluateTick(const BallTickInput& input,
                              std::vector<football::geometry::ColliderMotion>& scratch) const;

  football::ball::BallModel config_;
  const football::pitch::Pitch& pitch_;
  BallState state_;
  std::vector<football::geometry::ColliderMotion> tick_colliders_;
};

}
