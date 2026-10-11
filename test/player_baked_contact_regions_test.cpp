#include <catch2/catch_test_macros.hpp>
#include "sim/player/player_baked_pose_sample.hpp"
TEST_CASE("boot chest arm and palm regions preserve primary IDs and mirror geometry", "[baked-regions]") {
  PlayerBakedPoseSample start; start.state.position={3,2,0}; start.base_angle=.7f;
  for(auto& q:start.pose.orientations) q=blunted::Quaternion{};
  for(auto& p:start.pose.positions) p=blunted::Vector3(0);
  auto end=start; end.state.position+={.02f,.03f,0};
  end.pose.orientations[left_elbow].SetAngleAxis(.2f,{1,0,0});
  const auto motions=BuildBakedContactRegionMotions(start.ContactRegions(),end.ContactRegions(),7);
  auto reflected_start=start,reflected_end=end; reflected_start.Mirror(); reflected_end.Mirror();
  const auto reflected=BuildBakedContactRegionMotions(reflected_start.ContactRegions(),reflected_end.ContactRegions(),7);
  for(std::size_t index=0;index<motions.size();++index) {
    REQUIRE(motions[index].id==reflected[index].id);
    REQUIRE(motions[index].id>=kFirstBakedContactRegionId);
    REQUIRE(motions[index].id<kFirstDynamicBodyColliderId);
    for(std::size_t primary=0;primary<kBakedBodySegmentCount;++primary)
      REQUIRE(motions[index].id!=BakedBodyColliderId(7,static_cast<BakedBodySegment>(primary)));
    std::visit([&](const auto& shape) {
      using T=std::decay_t<decltype(shape)>; const auto& mirror=std::get<T>(reflected[index].end);
      if constexpr(std::is_same_v<T,football::ball::Capsule>) {
        auto a=shape.a,b=shape.b; a.Mirror(); b.Mirror();
        REQUIRE((a-mirror.a).GetLength()<1e-5f); REQUIRE((b-mirror.b).GetLength()<1e-5f);
        REQUIRE(shape.radius==mirror.radius);
      } else if constexpr(std::is_same_v<T,football::ball::Sphere>) {
        auto center=shape.center; center.Mirror(); REQUIRE((center-mirror.center).GetLength()<1e-5f);
        REQUIRE(shape.radius==mirror.radius);
      }
    },motions[index].end);
  }
  REQUIRE_THROWS_AS(BakedContactRegionId(UINT32_MAX,BakedContactRegion::Chest),std::out_of_range);
  REQUIRE_THROWS_AS(BakedContactRegionId(0,static_cast<BakedContactRegion>(9)),std::out_of_range);
}
