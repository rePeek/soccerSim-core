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

TEST_CASE("non-movement refinement is deterministic over the real library") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  std::vector<int> crude;
  for (uint32_t i = 0; i < library.Clips().size(); ++i) {
    crude.push_back(static_cast<int>(i));
  }

  MovementRefinementRequest request;
  request.rel_desired_direction = Vector3(0, -1, 0);
  request.desired_velocity = e_Velocity_Walk;
  request.look_at = Vector3(0, -10, 0);
  request.position = Vector3(0, 0, 0);
  request.angle = 0.0f;
  request.rel_incoming_body_direction = Vector3(0, -1, 0);
  request.incoming_velocity = e_Velocity_Idle;
  request.desired_foot = e_Foot_Left;
  request.idle_level = 0.0f;
  request.is_movement = false;
  request.is_ball_control = false;

  const auto first = RefineNonMovementCandidates(library, crude, request, /*strict=*/false,
                                                 0.94f, /*allowed_velocity_steps=*/1,
                                                 /*use_desired_trip_direction=*/false,
                                                 Vector3(0, -1, 0));
  const auto second = RefineNonMovementCandidates(library, crude, request, /*strict=*/false,
                                                  0.94f, 1, false, Vector3(0, -1, 0));
  CHECK(!first.empty());
  CHECK(first == second);

  // Strict selection keeps a single quadrant, like the Movement path.
  const auto strict = RefineNonMovementCandidates(library, crude, request, /*strict=*/true,
                                                  0.0f, 0, false, Vector3(0, -1, 0));
  CHECK(!strict.empty());
  const int quadrant = library.Get(static_cast<uint32_t>(strict.front())).metadata.quadrant;
  for (int id : strict) {
    CHECK(library.Get(static_cast<uint32_t>(id)).metadata.quadrant == quadrant);
  }
}
