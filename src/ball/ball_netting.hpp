#ifndef FOOTBALL_BALL_DETAIL_BALL_CONTACT_HPP
#define FOOTBALL_BALL_DETAIL_BALL_CONTACT_HPP

// Private to football::ball. Owns the discrete goal-net damping calculation.
// The surrounding free flight integration lives in ball_physics.hpp/.cpp.
// Posts and crossbars are ordinary static CCD colliders; there is no separate
// woodwork solver here.

#include "football/ball/ball_world.hpp"
#include "football/ball/ball_environment.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/ball/ball_model.hpp"

namespace football::ball::detail {

// Resolves side/rear/top goal netting in-place when the ball is behind the
// goal line inside the goal mouth.
void ResolveNetting(blunted::Vector3& position, blunted::Vector3& momentum,
                    const football::ball::GoalNetGeometry& net,
                    const football::ball::BallModel& config,
                    const football::ball::BallEnvironment& environment);

}  // namespace football::ball::detail

#endif  // FOOTBALL_BALL_DETAIL_BALL_CONTACT_HPP
