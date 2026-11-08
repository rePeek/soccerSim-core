#ifndef FOOTBALL_SIM_RULES_DENIED_GOAL_OPPORTUNITY_HPP
#define FOOTBALL_SIM_RULES_DENIED_GOAL_OPPORTUNITY_HPP

#include <span>

#include "football/contact/foul_assessment.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::sim::rules {

// The goal the victim was attacking, sampled by Runtime from the offender
// team's live defending direction. A rule function receives only values.
struct GoalContext {
  football::math::Vector3 goal_position;
};

// One opponent sampled at the contact instant. `goalkeeper` and `offender`
// identify the two actors the Law 12 test excludes from the covering check.
struct DefenderState {
  football::math::Vector3 position;
  bool goalkeeper = false;
  bool offender = false;
};

// Law 12 obvious goal-scoring opportunity: the victim is in range of the goal,
// in control of the ball, facing the goal, and no covering opponent (other than
// the goalkeeper and the offender) is closer to the goal than the victim.
// Pure value decision; Runtime only samples the live geometry.
bool IsObviousGoalScoringOpportunity(
    const football::sim::FoulAssessment& foul,
    const GoalContext& goal,
    std::span<const DefenderState> defenders);

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_DENIED_GOAL_OPPORTUNITY_HPP
