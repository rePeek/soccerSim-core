#ifndef FOOTBALL_SIM_BALL_BALL_ENVIRONMENT_HPP
#define FOOTBALL_SIM_BALL_BALL_ENVIRONMENT_HPP

namespace football::sim {

// Rule fact needed by the netting calculation, passed at each physics call.
// Ball does not retain this value or fetch it from a Match/Simulation owner.
struct BallEnvironment {
  bool ball_in_goal = false;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_BALL_BALL_ENVIRONMENT_HPP
