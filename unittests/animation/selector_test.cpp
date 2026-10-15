// ANIM-1 A1 — pure clip selection tests (BakedAnimationSelector).

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "animation_test_support.hpp"
#include "football/animation/selector.hpp"

namespace {
std::vector<AnimationClip> Clips() {
  std::vector<AnimationClip> clips;
  clips.push_back(MakeMovementClip(0, 0.0f, 0.0f, 0.0f, 0.0f));  // idle
  clips.push_back(MakeMovementClip(1, 0.0f, 3.5f, 0.0f, 1.0f));  // idle -> dribble
  clips.push_back(MakeMovementClip(2, 3.5f, 3.5f, 0.0f, 3.0f));  // dribble cruise
  AnimationClip shot = MakeMovementClip(3, 3.5f, 3.5f, 0.0f, 0.0f);
  shot.metadata.action_type = e_DefString_Shot;
  clips.push_back(shot);
  return clips;
}
}  // namespace

TEST_CASE("selection filters by function type") {
  const auto clips = Clips();
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Shot;
  DataSet out;
  BakedAnimationSelector::CrudeSelection(clips, query, out);
  REQUIRE(out.size() == 1);
  CHECK(out[0] == 3);
}

TEST_CASE("selection filters by incoming velocity bucket") {
  const auto clips = Clips();
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  DataSet out;
  BakedAnimationSelector::CrudeSelection(clips, query, out);

  REQUIRE_FALSE(out.empty());
  for (const int id : out) {
    CHECK(FloatToEnumVelocity(clips[static_cast<std::size_t>(id)].metadata.incoming_velocity) ==
          e_Velocity_Idle);
  }
  // The dribble-cruise clip (incoming 3.5) is not an idle candidate.
  for (const int id : out) CHECK(id != 2);
}

TEST_CASE("selection is deterministic for equal input") {
  const auto clips = Clips();
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  DataSet a;
  DataSet b;
  BakedAnimationSelector::CrudeSelection(clips, query, a);
  BakedAnimationSelector::CrudeSelection(clips, query, b);
  REQUIRE(a == b);
  REQUIRE_FALSE(a.empty());
}
