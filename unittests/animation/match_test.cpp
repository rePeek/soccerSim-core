// ANIM-2 A10.2 — the Animation::Match contract.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "football/animation/match.hpp"

using namespace football::math;
using football::animation::Match;
using football::animation::MatchQuery;
using football::animation::MotionEvaluator;
using football::animation::MotionPlanView;

namespace {

// The Movement/non-Movement refinement does not need the motion port yet, so
// this evaluator is never called.
struct NoMotion : MotionEvaluator {
  bool Evaluate(AnimationId, MotionPlanView&) override { return false; }
};

MatchQuery MovementQuery() {
  MatchQuery query;
  query.function_type = e_FunctionType_Movement;
  query.constraints.crude.byFunctionType = true;
  query.constraints.crude.functionType = e_FunctionType_Movement;
  query.constraints.crude.byIncomingVelocity = true;
  query.constraints.crude.incomingVelocity = e_Velocity_Idle;
  query.constraints.crude.incomingVelocity_Strict = true;
  query.constraints.crude.byIncomingBodyDirection = true;
  query.constraints.crude.incomingBodyDirection = Vector3(0, -1, 0);
  query.constraints.crude.incomingBodyDirection_Strict = true;

  query.refinement.rel_desired_direction = Vector3(0, -1, 0);
  query.refinement.desired_velocity = e_Velocity_Idle;
  query.refinement.cornering_bias = 0.0f;
  query.refinement.look_at = Vector3(0, -10, 0);
  query.refinement.position = Vector3(0, 0, 0);
  query.refinement.angle = 0.0f;
  query.refinement.rel_incoming_body_direction = Vector3(0, -1, 0);
  query.refinement.incoming_velocity = e_Velocity_Idle;
  query.refinement.desired_foot = e_Foot_Left;
  query.refinement.idle_level = 0.0f;
  query.refinement.use_desired_look_at = false;
  query.refinement.is_movement = true;
  return query;
}

}  // namespace

TEST_CASE("Match refines Movement candidates over the real library") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  REQUIRE(library.Clips().size() > 0);

  NoMotion motion;
  const auto result = Match(library, MovementQuery(), motion);
  REQUIRE(result.has_value());
  CHECK(!result->candidates.empty());
  CHECK(result->clip_id == result->candidates.front());
  CHECK(result->clip_id >= 0);
  CHECK(static_cast<std::size_t>(result->clip_id) < library.Clips().size());

  // The refinement is deterministic.
  const auto again = Match(library, MovementQuery(), motion);
  REQUIRE(again.has_value());
  CHECK(again->candidates == result->candidates);
}

TEST_CASE("Match reports no match when the crude constraint rejects everything") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  MatchQuery query = MovementQuery();
  // No clip carries this incoming special state, so the crude set is empty.
  query.constraints.crude.properties.incoming_special_state =
      "definitely-not-a-real-state";

  NoMotion motion;
  CHECK(!Match(library, query, motion).has_value());
}
