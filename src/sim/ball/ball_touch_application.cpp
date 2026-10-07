#include "sim/ball/ball_touch_application.hpp"

#include "sim/ball/ball.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/team/team.hpp"
#include "sim/player/possession.hpp"

namespace football::sim {
void ApplyBallTouch(Ball& ball, BallEnvironment environment, const blunted::Vector3& impulse,
                    std::span<MentalImage> history, Team& first, Team& second,
                    Tick now, const Player* retainer) {
  ball.Touch(impulse, environment);
  observation::RefreshLatestMentalImageBallPredictions(history, ball);
  player::RefreshTeamPossession(first, second, ball, now, retainer);
  player::RefreshTeamPossession(second, first, ball, now, retainer);
}
}  // namespace football::sim
