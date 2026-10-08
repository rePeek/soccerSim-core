#include "sim/ball_touch_application.hpp"

#include "football/ball/ball.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/team/team.hpp"
#include "sim/player/possession.hpp"

using football::ball::Ball;

namespace football::sim {
void ApplyBallTouch(Ball& ball, football::ball::BallEnvironment environment,
                    const blunted::Vector3& impulse,
                    std::span<MentalImage> history, Team& first, Team& second,
                    Tick now, const Player* retainer) {
  ball.Touch(impulse, environment);
  observation::RefreshLatestMentalImageBallPredictions(history, ball);
  player::RefreshTeamPossession(first, second, ball, now, retainer);
  player::RefreshTeamPossession(second, first, ball, now, retainer);
}
}  // namespace football::sim