#ifndef FOOTBALL_PLAYER_BAKED_CONTACT_REGIONS_HPP
#define FOOTBALL_PLAYER_BAKED_CONTACT_REGIONS_HPP
#include "player/execution/player_baked_body_pose.hpp"

// Separate band preserves the six original anatomical identities. Static pitch
// IDs do not occupy this band; overflow is checked before input construction.
inline constexpr football::geometry::ColliderId kFirstBakedContactRegionId=0x40000000u;
enum class BakedContactRegion : std::uint8_t {
  Chest, LeftBoot, RightBoot, LeftUpperArm, RightUpperArm,
  LeftForearm, RightForearm, LeftPalm, RightPalm
};
inline constexpr std::size_t kBakedContactRegionCount=9;
inline constexpr std::array<PlayerBodyPart,kBakedContactRegionCount> kBakedContactRegionParts{
  PlayerBodyPart::UpperBody,PlayerBodyPart::LowerBody,PlayerBodyPart::LowerBody,
  PlayerBodyPart::UpperBody,PlayerBodyPart::UpperBody,PlayerBodyPart::UpperBody,
  PlayerBodyPart::UpperBody,PlayerBodyPart::UpperBody,PlayerBodyPart::UpperBody};
inline football::geometry::ColliderId BakedContactRegionId(std::uint32_t slot,BakedContactRegion region) {
  const auto index=static_cast<std::uint8_t>(region);
  const std::uint64_t id=std::uint64_t(kFirstBakedContactRegionId)+std::uint64_t(slot)*kBakedContactRegionCount+index;
  if(index>=kBakedContactRegionCount || id>=kFirstDynamicBodyColliderId)
    throw std::out_of_range("baked contact region exceeds reserved band");
  return static_cast<football::geometry::ColliderId>(id);
}
inline std::array<football::geometry::ColliderShape,kBakedContactRegionCount> BuildBakedContactRegions(
    const PoseFrame& pose,const PlayerKinematicState& state,float base_angle,
    const football::player::PlayerBodyProfile& profile) {
  using namespace football::geometry;
  const float scale = profile.height_scale;
  const float girth = profile.girth_scale;
  if (!std::isfinite(girth) || girth <= 0.0f)
    throw std::invalid_argument("invalid baked contact region girth scale");
  const auto world=EvaluateBakedBodyPose(pose,state,base_angle,profile);
  const auto point=[&](BodyPart joint,football::math::Vector3 local) {
    return world.joints[joint]+world.orientations[joint]*(local*scale);
  };
  // Bounds calibrated offline against shipped trunk/foot/upperarm/lowerarm
  // vertices in their declared local joint spaces. Hand has no independent
  // bone/mesh: palm volume is the distal lowerarm subset, not a fabricated joint.
  return {Capsule{point(middle,{0,0,0}),point(middle,{0,0,.48f}),.233f*scale*BakedGirthScale(girth,kBakedGirthTorso)},
    Capsule{point(left_ankle,{0,.05f,-.057f}),point(left_ankle,{0,-.11f,-.057f}),.071f*scale*BakedGirthScale(girth,kBakedGirthBoot)},
    Capsule{point(right_ankle,{0,.05f,-.057f}),point(right_ankle,{0,-.11f,-.057f}),.071f*scale*BakedGirthScale(girth,kBakedGirthBoot)},
    Capsule{world.joints[left_shoulder],world.joints[left_elbow],.072f*scale*BakedGirthScale(girth,kBakedGirthUpperArm)},
    Capsule{world.joints[right_shoulder],world.joints[right_elbow],.072f*scale*BakedGirthScale(girth,kBakedGirthUpperArm)},
    Capsule{world.joints[left_elbow],point(left_elbow,{0,0,-.30f}),.053f*scale*BakedGirthScale(girth,kBakedGirthForearm)},
    Capsule{world.joints[right_elbow],point(right_elbow,{0,0,-.30f}),.053f*scale*BakedGirthScale(girth,kBakedGirthForearm)},
    Sphere{point(left_elbow,{0,-.005f,-.30f}),.065f*scale*BakedGirthScale(girth,kBakedGirthPalm)},
    Sphere{point(right_elbow,{0,-.005f,-.30f}),.065f*scale*BakedGirthScale(girth,kBakedGirthPalm)}};
}
inline std::array<football::geometry::ColliderMotion,kBakedContactRegionCount> BuildBakedContactRegionMotions(
    const std::array<football::geometry::ColliderShape,kBakedContactRegionCount>& start,
    const std::array<football::geometry::ColliderShape,kBakedContactRegionCount>& end,std::uint32_t slot) {
  std::array<football::geometry::ColliderMotion,kBakedContactRegionCount> out;
  for(std::size_t index=0;index<out.size();++index)
    out[index]={BakedContactRegionId(slot,static_cast<BakedContactRegion>(index)),start[index],end[index],kPlayerBodyContactMaterial};
  return out;
}
#endif
