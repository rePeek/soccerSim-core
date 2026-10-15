// ANIM-1 A6.3b.2 — pure Movement candidate refinement tests.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "football/animation/movement_selection.hpp"


using football::math::Vector3;

TEST_CASE("the moved reference directions match the legacy snap values") {
  CHECK(ForceIntoAllowedBodyDirectionAngle(0.01f) == 0.0f);
  CHECK(ForceIntoPreferredDirectionAngle(0.01f) == 0.0f);
  CHECK(AllowedBodyDirVecs().size() == 5);
  CHECK(PreferredDirectionVecs().size() == 11);
  const Vector3 snapped = ForceIntoAllowedBodyDirectionVec(Vector3(0, -1, 0));
  CHECK(snapped.GetDotProduct(Vector3(0, -1, 0)) > 0.99f);
}

TEST_CASE("movement refinement is deterministic and keeps one body quadrant") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  REQUIRE(library.Clips().size() > 0);

  std::vector<int> crude;
  for (uint32_t i = 0; i < library.Clips().size(); ++i) {
    crude.push_back(static_cast<int>(i));
  }

  MovementRefinementRequest request;
  request.rel_desired_direction = Vector3(0, -1, 0);
  request.desired_velocity = e_Velocity_Sprint;
  request.look_at = Vector3(0, -10, 0);
  request.position = Vector3(0, 0, 0);
  request.angle = 0.0f;
  request.rel_incoming_body_direction = Vector3(0, -1, 0);
  request.incoming_velocity = e_Velocity_Idle;
  request.desired_foot = e_Foot_Left;
  request.idle_level = 0.0f;
  request.use_desired_look_at = false;

  const std::vector<int> first = RefineMovementCandidates(library, crude, request);
  const std::vector<int> second = RefineMovementCandidates(library, crude, request);

  CHECK(!first.empty());
  CHECK(first == second);

  const int quadrant =
      library.Get(static_cast<uint32_t>(first.front())).metadata.quadrant;
  for (int id : first) {
    CHECK(library.Get(static_cast<uint32_t>(id)).metadata.quadrant == quadrant);
  }
}
