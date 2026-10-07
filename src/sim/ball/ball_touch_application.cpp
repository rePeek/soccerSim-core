#include "sim/ball/ball_touch_application.hpp"

#include "sim/ball/ball.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/team/team.hpp"

namespace football::sim {
void ApplyBallTouch(Ball& ball, BallEnvironment environment, const blunted::Vector3& impulse,
                    std::span<MentalImage> history, Team& first, Team& second) {
  ball.Touch(impulse, environment);
  observation::RefreshLatestMentalImageBallPredictions(history, ball);
  first.UpdatePossessionStats();
  second.UpdatePossessionStats();
}
}  // namespace football::sim
