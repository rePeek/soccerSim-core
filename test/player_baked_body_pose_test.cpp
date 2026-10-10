#include <catch2/catch_test_macros.hpp>
#include "sim/player/player_baked_body_pose.hpp"
#include "sim/testing/simulation_access.hpp"
#include "app/fixtures/default_teams.hpp"

using blunted::Vector3;
using football::sim::testing::SimulationAccess;

TEST_CASE("baked FK retains bind hierarchy and excludes already integrated root XY", "[baked-pose]") {
  PoseFrame pose{};
  for (auto& q : pose.orientations) q = blunted::Quaternion{};
  for (auto& p : pose.positions) p = Vector3(0);
  pose.positions[BodyPart::player] = {100, 200, -.5f};
  PlayerKinematicState state; state.position = {3, 4, 0};
  const auto world = EvaluateBakedBodyPose(pose, state, 0);
  REQUIRE((world.joints[body] - Vector3(3, 4, .46f)).GetLength() < 1e-6f);
  REQUIRE((world.joints[neck] - Vector3(3, 3.97f, 1.11f)).GetLength() < 1e-6f);
  REQUIRE(std::fabs((world.joints[left_knee] - world.joints[left_thigh]).GetLength() - .42f) < 1e-6f);
  // Non-root keyframe positions are not bind offsets (Animation::Apply ignores them).
  pose.positions[left_knee] = {123, 456, 789};
  REQUIRE(EvaluateBakedBodyPose(pose, state, 0).joints[left_knee] == world.joints[left_knee]);
  auto mirrored_state = state; mirrored_state.position.Mirror();
  const auto mirrored = EvaluateBakedBodyPose(pose, mirrored_state, blunted::pi);
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    auto expected = world.joints[joint]; expected.Mirror();
    REQUIRE((expected - mirrored.joints[joint]).GetLength() < 2e-6f);
  }
  REQUIRE_THROWS_AS(EvaluateBakedBodyPose(pose, state, 0, 0), std::invalid_argument);
}

TEST_CASE("real baked Sliding and Trip produce posed anatomy rather than fixed low volumes", "[baked-pose]") {
  Simulation s;
  MatchOptions options; options.snapshot_capacity = 1;
  s.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, options);
  const auto& library = SimulationAccess::AnimationLibraryOf(s);
  PlayerKinematicState state; state.position = {5, 2, 0};
  std::size_t sliding_frames = 0, trip_frames = 0;
  float min_head = 10, max_head = -10;
  for (const auto& clip : library.Clips()) {
    if (clip.metadata.action_type != e_FunctionType_Sliding &&
        clip.metadata.action_type != e_FunctionType_Trip) continue;
    for (const auto& frame : clip.poses) {
      if (clip.metadata.action_type == e_FunctionType_Sliding) ++sliding_frames;
      else ++trip_frames;
      const auto world = EvaluateBakedBodyPose(frame, state, .3f);
      const auto shapes = BuildBodyCollider(frame, state, .3f);
      min_head = std::min(min_head, world.joints[neck].coords[2]);
      max_head = std::max(max_head, world.joints[neck].coords[2]);
      const auto& lower_leg = std::get<football::ball::Capsule>(shapes.shapes[3]);
      REQUIRE(lower_leg.a == world.joints[left_knee]);
      REQUIRE(lower_leg.b == world.joints[left_ankle]);
      REQUIRE(std::get<football::ball::Sphere>(shapes.shapes[1]).center == world.joints[neck]);
      for (const auto& joint : world.joints) {
        REQUIRE(std::isfinite(joint.coords[0]));
        REQUIRE(std::isfinite(joint.coords[1]));
        REQUIRE(std::isfinite(joint.coords[2]));
      }
    }
  }
  REQUIRE(sliding_frames > 0);
  REQUIRE(trip_frames > 0);
  REQUIRE(min_head < 1.0f);
  REQUIRE(max_head - min_head > .5f);
}
