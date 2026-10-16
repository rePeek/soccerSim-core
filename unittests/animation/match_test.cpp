// ANIM-2 A10.2 — the Animation::Match contract.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "football/animation/match.hpp"

using namespace football::math;
using football::animation::Match;
using football::animation::MatchQuery;

namespace {

MatchQuery MovementQuery() {
  MatchQuery query;
  query.function_type = e_FunctionType_Movement;
  query.branch = football::animation::MatchBranch::Movement;
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

  const auto result = Match(library, MovementQuery());
  REQUIRE(result.has_value());
  CHECK(!result->candidates.empty());
  CHECK(result->clip_id == result->candidates.front());
  CHECK(result->clip_id >= 0);
  CHECK(static_cast<std::size_t>(result->clip_id) < library.Clips().size());

  // The refinement is deterministic.
  const auto again = Match(library, MovementQuery());
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

  CHECK(!Match(library, query).has_value());
}

TEST_CASE("Match applies the Movement preference and returns the final clip") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  const auto plain = Match(library, MovementQuery());
  REQUIRE(plain.has_value());
  REQUIRE(plain->candidates.size() > 1);

  // A legal preferred candidate wins, regardless of where it sorted.
  const int preferred = plain->candidates.back();
  REQUIRE(preferred != plain->clip_id);
  MatchQuery query = MovementQuery();
  query.apply_movement_preference = true;
  query.preferred_movement_clip = preferred;
  const auto chosen = Match(library, query);
  REQUIRE(chosen.has_value());
  CHECK(chosen->clip_id == preferred);
  CHECK(chosen->candidates.front() == preferred);

  // The entry frame is matched against that FINAL clip, not the old front.
  const AnimationClip& preferred_clip =
      library.Get(static_cast<uint32_t>(preferred));
  constexpr int kEntry = 3;
  REQUIRE(static_cast<int>(preferred_clip.poses.size()) > kEntry + 2);
  query.entry.enabled = true;
  query.entry.reference_pose = preferred_clip.poses[kEntry];
  const auto entry = Match(library, query);
  REQUIRE(entry.has_value());
  CHECK(entry->clip_id == preferred);
  // The frame is legal for the FINAL clip (not the pre-reorder front).
  CHECK(entry->start_frame >= 0);
  CHECK(entry->start_frame <=
        static_cast<int>(preferred_clip.poses.size()) - 2);

  // An illegal preference falls back to the legacy front candidate.
  MatchQuery fallback = MovementQuery();
  fallback.apply_movement_preference = true;
  fallback.preferred_movement_clip = -12345;
  const auto fallback_result = Match(library, fallback);
  REQUIRE(fallback_result.has_value());
  CHECK(fallback_result->clip_id == plain->clip_id);
}
