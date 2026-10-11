#include <catch2/catch_test_macros.hpp>
#include "sim/player/player_baked_pose_realization.hpp"
namespace {
PlayerBakedPoseSample Bind() {
  PlayerBakedPoseSample p;
  for(auto& q:p.pose.orientations) q=blunted::Quaternion{};
  for(auto& v:p.pose.positions) v=blunted::Vector3(0);
  p.state.position={5,2,0}; p.base_angle=.3f;
  return p;
}
float AngularDistance(blunted::Quaternion a, const blunted::Quaternion& b) {
  return 2*std::acos(blunted::clamp(a.MakeSameNeighborhood(b),-1.f,1.f));
}
}
TEST_CASE("baked realization smooths the model pose instead of filtering its colliders", "[baked-pose][pose-realization]") {
  auto old=WorldBakedPose(Bind()), target=Bind(); target.base_angle=2.8f;
  for(std::size_t j=0;j<kBodyPartCount;++j) if(j!=BodyPart::player)
    target.pose.orientations[j].SetAngleAxis(1.2f,{1,0,0});
  target.pose.positions[BodyPart::player]={100,200,-.5f}; target.sampled_frame=0;
  target.state.position={5.01f,2.02f,0};
  const auto actual=RealizeBakedPose(old,target);
  REQUIRE(actual.state.position==target.state.position);
  REQUIRE(actual.base_angle==0);
  REQUIRE(actual.pose.positions[BodyPart::player].coords[2]<0);
  REQUIRE(actual.pose.positions[BodyPart::player].coords[2]>=-.02801f);
  const auto desired=WorldBakedPose(target);
  for(std::size_t j=0;j<kBodyPartCount;++j) if(j!=BodyPart::player) {
    float factor=1;
    if(j==left_elbow || j==right_elbow || j==left_knee || j==right_knee) factor=1.2f;
    if(j==left_ankle || j==right_ankle) factor=1.6f;
    const float limit=.3f*7.5f*blunted::pi*football::sim::kTickSeconds*factor;
    REQUIRE(AngularDistance(actual.pose.orientations[j],old.pose.orientations[j])<=limit+3e-5f);
    REQUIRE(AngularDistance(actual.pose.orientations[j],desired.pose.orientations[j])<
        AngularDistance(old.pose.orientations[j],desired.pose.orientations[j]));
  }
  const auto world=EvaluateBakedBodyPose(actual.pose,actual.state,actual.base_angle,actual.scale);
  REQUIRE(std::fabs((world.joints[left_knee]-world.joints[left_thigh]).GetLength()-.42f)<1e-6f);
  REQUIRE(std::fabs((world.joints[right_knee]-world.joints[right_thigh]).GetLength()-.42f)<1e-6f);
  auto mirror_old=old, mirror_target=target; mirror_old.Mirror(); mirror_target.Mirror();
  const auto mirror=RealizeBakedPose(mirror_old,mirror_target);
  const auto reflected=EvaluateBakedBodyPose(mirror.pose,mirror.state,mirror.base_angle,mirror.scale);
  for(std::size_t joint=0;joint<kBodyPartCount;++joint) {
    auto expected=world.joints[joint]; expected.Mirror();
    REQUIRE((expected-reflected.joints[joint]).GetLength()<1e-5f);
  }
  auto special=RealizeBakedPose(old,target,true);
  REQUIRE(special.pose.positions[BodyPart::player].coords[2]>=-.06001f);
  REQUIRE(special.pose.positions[BodyPart::player].coords[2]<actual.pose.positions[BodyPart::player].coords[2]);
  target.scale=0; REQUIRE_THROWS_AS(RealizeBakedPose(old,target),std::invalid_argument);
}
