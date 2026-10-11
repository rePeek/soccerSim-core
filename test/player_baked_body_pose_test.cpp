#include <algorithm>
#include <limits>
#include <vector>
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
    const auto action=StringToFunctionType(static_cast<e_DefString>(clip.metadata.action_type));
    if (action != e_FunctionType_Sliding && action != e_FunctionType_Trip) continue;
    for (const auto& frame : clip.poses) {
      if (action == e_FunctionType_Sliding) ++sliding_frames;
      else ++trip_frames;
      const auto world = EvaluateBakedBodyPose(frame, state, .3f);
      const auto shapes = BuildBodyCollider(frame, state, .3f);
      min_head = std::min(min_head, world.joints[neck].coords[2]);
      max_head = std::max(max_head, world.joints[neck].coords[2]);
      const auto& lower_leg = std::get<football::ball::Capsule>(shapes.shapes[3]);
      REQUIRE(lower_leg.a == world.joints[left_knee]);
      REQUIRE(lower_leg.b == world.joints[left_ankle]);
      const auto& head_shape=std::get<football::ball::Sphere>(shapes.shapes[1]);
      REQUIRE(head_shape.center == world.joints[neck]+world.orientations[neck]*kBakedHeadCenter);
      REQUIRE(head_shape.radius == kBakedHeadRadius);
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

TEST_CASE("six anatomical segments have disjoint fixed-slot physics identities", "[baked-pose][deforming-ccd]") {
  using namespace football::ball;
  PoseFrame pose{};
  for (auto& q : pose.orientations) q = blunted::Quaternion{};
  for (auto& p : pose.positions) p = Vector3(0);
  PlayerKinematicState state; state.position = {2,3,0};
  const auto body = BuildBodyCollider(pose,state,.3f);
  std::vector<ColliderId> ids;
  for (std::uint32_t slot = 0; slot < 100; ++slot) {
    const auto coarse = PlayerBodyColliderIdsForSlot(slot);
    const auto motions = BuildBakedBodyColliderMotions(body,body,slot);
    REQUIRE(motions.size() == 6);
    for (std::size_t i = 0; i < motions.size(); ++i) {
      const auto id = motions[i].id;
      REQUIRE(id >= kFirstBakedBodyColliderId);
      for (auto old : coarse.values) REQUIRE(id != old);
      REQUIRE(id == BakedBodyColliderId(slot,static_cast<BakedBodySegment>(i)));
      REQUIRE(std::find(ids.begin(),ids.end(),id) == ids.end());
      ids.push_back(id);
      REQUIRE(body.parts[i] == (i == 0 ? PlayerBodyPart::UpperBody :
          i == 1 ? PlayerBodyPart::Head : PlayerBodyPart::LowerBody));
    }
  }
  // Reversed traversal and coordinate reflection never allocate new identities.
  for (std::uint32_t slot = 100; slot-- > 0;) {
    auto reflected = state; reflected.position.Mirror();
    const auto mirror = BuildBodyCollider(pose,reflected,.3f+blunted::pi);
    const auto motions = BuildBakedBodyColliderMotions(mirror,mirror,slot);
    for (std::size_t i = 0; i < motions.size(); ++i) REQUIRE(motions[i].id == ids[slot*6+i]);
  }
  const auto max_slot = std::uint32_t((std::uint64_t(UINT32_MAX)-kFirstBakedBodyColliderId-5)/6);
  REQUIRE_NOTHROW(BuildBakedBodyColliderMotions(body,body,max_slot));
  REQUIRE_THROWS_AS(BuildBakedBodyColliderMotions(body,body,max_slot+1),std::out_of_range);
  REQUIRE_THROWS_AS(BakedBodyColliderId(0,static_cast<BakedBodySegment>(6)),std::out_of_range);
  REQUIRE_THROWS_AS(PlayerBodyColliderId(0,static_cast<PlayerBodyPart>(3)),std::out_of_range);
  REQUIRE_THROWS_AS(PlayerBodyColliderIdsForSlot(UINT32_MAX),std::out_of_range);
  REQUIRE_THROWS_AS(BakedBodyColliderId(UINT32_MAX,BakedBodySegment::Torso),std::out_of_range);
}

TEST_CASE("real adjacent baked pose frames produce six endpoint motion tapes", "[baked-pose][deforming-ccd]") {
  using namespace football::ball;
  Simulation s; MatchOptions options; options.snapshot_capacity = 1;
  s.Init(football::app::fixtures::MakeDefaultHomeTeam(),
         football::app::fixtures::MakeDefaultAwayTeam(),football::model::Pitch{},options);
  const auto& library = SimulationAccess::AnimationLibraryOf(s);
  std::size_t intervals = 0;
  PlayerKinematicState start, end; start.position = {5,2,0}; end.position = {5.02f,2.01f,0};
  for (const auto& clip : library.Clips()) {
    const auto action=StringToFunctionType(static_cast<e_DefString>(clip.metadata.action_type));
    if (action != e_FunctionType_Sliding && action != e_FunctionType_Trip) continue;
    for (std::size_t frame = 1; frame < clip.poses.size(); ++frame) {
      ++intervals;
      const auto a = BuildBodyCollider(clip.poses[frame-1],start,.3f);
      const auto b = BuildBodyCollider(clip.poses[frame],end,.31f);
      const auto motions = BuildBakedBodyColliderMotions(a,b,17);
      for (std::size_t segment = 0; segment < motions.size(); ++segment) {
        REQUIRE(motions[segment].id == BakedBodyColliderId(17,static_cast<BakedBodySegment>(segment)));
        REQUIRE(motions[segment].start.index() == a.shapes[segment].index());
        std::visit([&](const auto& begin) {
          using T = std::decay_t<decltype(begin)>;
          const auto& finish = std::get<T>(motions[segment].end);
          const auto& actual_start = std::get<T>(a.shapes[segment]);
          const auto& actual_end = std::get<T>(b.shapes[segment]);
          if constexpr (std::is_same_v<T,Capsule>) {
            REQUIRE(begin.a == actual_start.a); REQUIRE(begin.b == actual_start.b);
            REQUIRE(finish.a == actual_end.a); REQUIRE(finish.b == actual_end.b);
          } else if constexpr (std::is_same_v<T,Sphere>) {
            REQUIRE(begin.center == actual_start.center); REQUIRE(finish.center == actual_end.center);
          }
          if constexpr (!std::is_same_v<T,Plane>) {
            REQUIRE(begin.radius == actual_start.radius); REQUIRE(finish.radius == actual_end.radius);
          }
        },motions[segment].start);
      }
    }
  }
  REQUIRE(intervals > 100);
}
