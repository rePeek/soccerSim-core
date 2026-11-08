#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "referee/denied_goal_opportunity.hpp"

namespace {

using football::math::Vector3;
using football::sim::FoulAssessment;
using football::sim::rules::DefenderState;
using football::sim::rules::GoalContext;
using football::sim::rules::IsObviousGoalScoringOpportunity;

FoulAssessment Attack() {
  FoulAssessment foul;
  foul.victim_team_id = 0;
  foul.offender_team_id = 1;
  foul.victim_position = Vector3(40.0f, 0.0f, 0.0f);
  foul.ball_position = Vector3(40.0f, 0.0f, 0.0f);
  foul.victim_direction = Vector3(1.0f, 0.0f, 0.0f);
  return foul;
}

GoalContext Goal() { return GoalContext{Vector3(52.5f, 0.0f, 0.0f)}; }

}  // namespace

TEST_CASE("DOGSO: a clear run on goal with no cover is an opportunity",
          "[referee][dogso]") {
  const std::vector<DefenderState> cover{
      {Vector3(20.0f, 0.0f, 0.0f), false, false}};
  CHECK(IsObviousGoalScoringOpportunity(Attack(), Goal(), cover));
}

TEST_CASE("DOGSO: a covering outfield defender removes the opportunity",
          "[referee][dogso]") {
  const std::vector<DefenderState> cover{
      {Vector3(45.0f, 0.0f, 0.0f), false, false}};
  CHECK_FALSE(IsObviousGoalScoringOpportunity(Attack(), Goal(), cover));
}

TEST_CASE("DOGSO: the goalkeeper and the offender are not covering defenders",
          "[referee][dogso]") {
  const std::vector<DefenderState> cover{
      {Vector3(50.0f, 0.0f, 0.0f), true, false},
      {Vector3(45.0f, 0.0f, 0.0f), false, true}};
  CHECK(IsObviousGoalScoringOpportunity(Attack(), Goal(), cover));
}

TEST_CASE("DOGSO: range, ball distance and facing are all required",
          "[referee][dogso]") {
  const std::vector<DefenderState> no_cover;

  FoulAssessment far = Attack();
  far.victim_position = Vector3(10.0f, 0.0f, 0.0f);
  CHECK_FALSE(IsObviousGoalScoringOpportunity(far, Goal(), no_cover));

  FoulAssessment away_from_ball = Attack();
  away_from_ball.ball_position = Vector3(20.0f, 0.0f, 0.0f);
  CHECK_FALSE(IsObviousGoalScoringOpportunity(away_from_ball, Goal(), no_cover));

  FoulAssessment facing_away = Attack();
  facing_away.victim_direction = Vector3(-1.0f, 0.0f, 0.0f);
  CHECK_FALSE(IsObviousGoalScoringOpportunity(facing_away, Goal(), no_cover));
}
