#include "referee/denied_goal_opportunity.hpp"

#include <cmath>

namespace football::sim::rules {
namespace {

// Engineering ranges, not calibrated distributions. Kept as named constants so
// the rule can be tested without a full Simulation.
constexpr float kMaxVictimToGoal = 30.0f;
constexpr float kMaxVictimToBall = 10.0f;

}  // namespace

bool IsObviousGoalScoringOpportunity(
    const football::sim::FoulAssessment& foul,
    const GoalContext& goal,
    std::span<const DefenderState> defenders) {
  const football::math::Vector3 victim = foul.victim_position.Get2D();
  const football::math::Vector3 goal_position = goal.goal_position.Get2D();
  const float range = (goal_position - victim).GetLength();
  if (range > kMaxVictimToGoal) return false;
  if ((victim - foul.ball_position.Get2D()).GetLength() > kMaxVictimToBall)
    return false;
  // The victim must be oriented toward the goal they are attacking.
  if (foul.victim_direction.GetDotProduct(
          (goal_position - victim).GetNormalized(0)) < 0.0f)
    return false;
  // No covering outfield opponent is closer to the goal than the victim.
  for (const DefenderState& defender : defenders) {
    if (defender.goalkeeper || defender.offender) continue;
    if ((goal_position - defender.position.Get2D()).GetLength() < range)
      return false;
  }
  return true;
}

}  // namespace football::sim::rules
