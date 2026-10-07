#ifndef FOOTBALL_SIM_RULES_GOAL_HPP
#define FOOTBALL_SIM_RULES_GOAL_HPP

#include "foundation/math/vector3.hpp"
#include "model/pitch.hpp"

namespace football::sim {

// Legacy goal-mouth segment intersection in a shared physical frame; side is ±1.
// Preserves strict segment endpoints, both crossing directions and side-net veto.
// Live-ball authorization and the prediction lookahead gate belong to the caller;
// this predicate neither reads actors/time nor applies score or scorer facts.
bool CrossedGoalLine(const football::model::Pitch& pitch, int side,
                     const blunted::Vector3& previous,
                     const blunted::Vector3& current);

}  // namespace football::sim

#endif
