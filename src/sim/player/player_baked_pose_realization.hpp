#ifndef FOOTBALL_PLAYER_BAKED_POSE_REALIZATION_HPP
#define FOOTBALL_PLAYER_BAKED_POSE_REALIZATION_HPP
#include "sim/player/player_baked_pose_sample.hpp"
#include "football/foundation/time/tick_boundary.hpp"

// Animation realization, NOT a collision filter. Both observable skeleton and
// physical primitives must use this returned pose. The policy is adapted from the
// original Animation::Apply simple smoothing branch, but does NOT slerp joint
// rotation toward the previous realized pose during clip entry: that blend makes
// a moving target perpetually lag and then releases the accumulated lag at the
// end of the entry window as a non-physical surface-speed spike (e.g. a sprint
// ankle swinging 0.215 m in one Tick where the raw gait moves only ~0.04 m).
// Entry smoothing is instead the bounded rotation-rate ramp below, so the
// realized pose never jumps faster than the per-joint rotational limit.
// Root XY remains Player-owned. No controller/action/RNG execution occurs here.
inline PlayerBakedPoseSample WorldBakedPose(PlayerBakedPoseSample target) {
  football::math::Quaternion base; base.SetAngleAxis(target.base_angle,{0,0,1});
  target.pose.orientations[BodyPart::body] =
      (base*target.pose.orientations[BodyPart::body]).GetNormalized();
  target.base_angle=0;
  return target;
}
inline PlayerBakedPoseSample RealizeBakedPose(const PlayerBakedPoseSample& previous,
    const PlayerBakedPoseSample& input, bool outgoing_special_state = false) {
  auto next=WorldBakedPose(input);
  const auto old=WorldBakedPose(previous);
  if (!(next.body_profile.height_scale>0) || !std::isfinite(next.body_profile.height_scale)
      || next.body_profile.height_scale!=old.body_profile.height_scale)
    throw std::invalid_argument("baked realization requires a fixed positive actor height scale");
  const float dt=football::sim::kTickSeconds;
  const float begin=std::sqrt(football::math::curve(1-football::math::NormalizedClamp(float(input.sampled_frame),0,8),1));
  const float bias=begin*.5f;
  for(std::size_t joint=0;joint<kBodyPartCount;++joint) {
    if(joint==BodyPart::player) continue;
    auto desired=next.pose.orientations[joint];
    const auto& current=old.pose.orientations[joint];
    desired.MakeSameNeighborhood(current);
    // No slerp toward `current` here: the rate limit below is the smoothing.
    // Blending toward the previous pose would accumulate entry lag and release
    // it when `begin` reaches zero (frame 8), fabricating unreal limb speed.
    const float dot=desired.MakeSameNeighborhood(current);
    const float angle=2*std::acos(football::math::clamp(dot,-1.f,1.f));
    float rate=7.5f*football::math::pi;
    if(joint==left_elbow || joint==right_elbow || joint==left_knee || joint==right_knee) rate*=1.2f;
    if(joint==left_ankle || joint==right_ankle) rate*=1.6f;
    const float allowed=(.3f+.7f*(1-begin))*rate*dt;
    if(angle>allowed) {
      const auto delta=current.GetRotationTo(desired).GetNormalized();
      desired=(delta.GetRotationMultipliedBy(allowed/angle)*current).GetNormalized();
    }
    next.pose.orientations[joint]=desired;
  }
  if(begin>.01f) {
    const float old_z=old.pose.positions[BodyPart::player].coords[2]*old.body_profile.height_scale;
    float z=next.pose.positions[BodyPart::player].coords[2]*next.body_profile.height_scale;
    z=z*(1-bias)+old_z*bias;
    const float allowed=(outgoing_special_state?6.f:2.8f)*dt;
    const float limited=old_z+football::math::clamp(z-old_z,-allowed,allowed);
    z=limited*begin+z*(1-begin);
    next.pose.positions[BodyPart::player].coords[2]=z/next.body_profile.height_scale;
  }
  return next;
}
#endif
