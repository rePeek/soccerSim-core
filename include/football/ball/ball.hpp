#pragma once

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/models.h"

namespace football::pitch { class Pitch; }

namespace football::ball {

using namespace football::pitch;

struct BallState {
  math::Vector3 position;
  math::Vector3 velocity;
  math::Vector3 angular_velocity;
  math::Quaternion orientation;
};

// Defined in ball_contact.hpp together with BallContact and AdvanceBallTick.
struct BallStepResult;

class Ball {
 public:
  explicit Ball(const BallModel& model =
      ::football_ball_model{0.43f, 0.11f, 0.02f});

  BallState get_state() const { return state_; }
  void set_state(const BallState& state) { state_ = state; }
  const BallModel& model() const { return model_; }

  // The pitch is the scene a tick resolves against, so it is a per-call
  // argument: Ball owns no reference to it and outlives no owner.
  BallStepResult Step(const Pitch& pitch);
  BallStepResult Predict(const Pitch& pitch) const;
  BallState Predict(const Pitch& pitch,
                    sim::TickSpan ahead) const;

  // Authoritative ground placement: z is forced to the ball radius and
  // velocity/spin are cleared. Airborne states still use set_state(BallState).
  void PlaceOnGround(float x, float y);

 private:
  BallStepResult EvaluateTick(const Pitch& pitch) const;

  BallModel model_;
  BallState state_;
};

}  // namespace football::ball
