#include "ball_solver.hpp"

#include "core/physics/contact/ball/ball_collision_solver.hpp"
#include "core/physics/movement/ball/ball_movement.hpp"

namespace football_sim::physics {

BallPhysicsStepResult SolveBallTick(
    football_sim::Ball& ball, float dt, bool applyWoodwork,
    const football_sim::contact::GoalGeometry& goal,
    const std::vector<football_sim::contact::PlayerBodyCandidate>& players) {
  const football_sim::BallState current = ball.State();

  const football_sim::BallState afterForces = movement::ball::ApplyForces(
      current, ball, dt, movement::ball::BallMovementParameters{});

  const football_sim::contact::BallContactStepResult contactResult =
      football_sim::contact::SolveBallContacts(afterForces, ball, dt,
                                               applyWoodwork, goal, players);

  const football_sim::BallState finalState =
      movement::ball::IntegrateOrientation(contactResult.state, dt);
  ball.SetState(finalState);

  BallPhysicsStepResult result;
  result.impacts = contactResult.impacts;
  result.impactCount = contactResult.impactCount;
  return result;
}

}  // namespace football_sim::physics