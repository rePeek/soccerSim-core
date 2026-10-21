#ifndef FOOTBALL_SIM_RULES_GOAL_HPP
#define FOOTBALL_SIM_RULES_GOAL_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"

namespace football::sim {

// Full-ball goal-mouth segment intersection in a shared physical frame; side is
// ±1. `ball_radius` is the physical sphere radius: the ball is only a goal when
// its whole body crossed the line, so the planes are offset by one radius.
// Preserves strict segment endpoints, both crossing directions and the side-net
// veto. Live-ball authorization and the prediction lookahead gate belong to the
// caller; this predicate neither reads actors/time nor applies score/scorer.
bool CrossedGoalLine(const football::pitch::Pitch& pitch, int side,
                     const football::math::Vector3& previous,
                     const football::math::Vector3& current, float ball_radius);

}  // namespace football::sim

#endif
