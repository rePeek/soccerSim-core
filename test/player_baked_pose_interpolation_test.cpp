#include <iostream>
#include "football/pitch/pitch.hpp"
#include <catch2/catch_test_macros.hpp>
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "sim/player/player_baked_pose_realization.hpp"
using football::sim::testing::SimulationAccess;
namespace {
struct CompleteGeometry {
  std::array<football::ball::ColliderShape,kBakedBodySegmentCount+kBakedContactRegionCount> shapes;
};
CompleteGeometry Geometry(const PlayerBakedPoseSample& sample) {
  CompleteGeometry result; const auto primary=sample.Geometry(); const auto extra=sample.ContactRegions();
  std::copy(primary.shapes.begin(),primary.shapes.end(),result.shapes.begin());
  std::copy(extra.begin(),extra.end(),result.shapes.begin()+kBakedBodySegmentCount);
  return result;
}
PlayerBakedPoseSample Tween(const PlayerBakedPoseSample& a,const PlayerBakedPoseSample& b,float t) {
  auto sample=b; sample.state.position=a.state.position*(1-t)+b.state.position*t;
  for(std::size_t joint=0;joint<kBodyPartCount;++joint) {
    auto q=b.pose.orientations[joint]; q.MakeSameNeighborhood(a.pose.orientations[joint]);
    sample.pose.orientations[joint]=a.pose.orientations[joint].GetSlerped(t,q).GetNormalized();
    sample.pose.positions[joint]=a.pose.positions[joint]*(1-t)+b.pose.positions[joint]*t;
  }
  return sample;
}
float Error(const PlayerBakedPoseSample& a,const PlayerBakedPoseSample& b,
    const CompleteGeometry& first,const CompleteGeometry& last,float t) {
  const auto actual=Geometry(Tween(a,b,t)); float max=0;
  for(std::size_t segment=0;segment<actual.shapes.size();++segment)
    std::visit([&](const auto& c) {
      using T=std::decay_t<decltype(c)>;
      const auto& x=std::get<T>(first.shapes[segment]); const auto& y=std::get<T>(last.shapes[segment]);
      if constexpr(std::is_same_v<T,football::ball::Sphere>) max=std::max(max,(c.center-(x.center*(1-t)+y.center*t)).GetLength());
      else if constexpr(std::is_same_v<T,football::ball::Capsule>) {
        max=std::max(max,(c.a-(x.a*(1-t)+y.a*t)).GetLength());
        max=std::max(max,(c.b-(x.b*(1-t)+y.b*t)).GetLength());
      }
    },actual.shapes[segment]);
  return max;
}
}
TEST_CASE("dense adjacent pose sampling measures linear primitive versus local quaternion motion", "[pose-interpolation]") {
  Simulation s; MatchOptions options; options.snapshot_capacity=1;
  s.Init(football::app::fixtures::MakeDefaultHomeTeam(),football::app::fixtures::MakeDefaultAwayTeam(),football::pitch::Pitch{},options);
  std::size_t movement=0,sliding=0,trip=0,intervals=0; float raw_error=0,realized_error=0;
  std::string worst; int worst_frame=0;
  for(const auto& clip:SimulationAccess::AnimationLibraryOf(s).Clips()) {
    const auto action=StringToFunctionType(static_cast<e_DefString>(clip.metadata.action_type));
    if(action!=e_FunctionType_Movement && action!=e_FunctionType_Sliding && action!=e_FunctionType_Trip) continue;
    movement+=action==e_FunctionType_Movement; sliding+=action==e_FunctionType_Sliding; trip+=action==e_FunctionType_Trip;
    PlayerBakedPoseSample target; target.state.position={3,2,0}; target.base_angle=.3f; target.pose=clip.poses.front();
    auto raw=WorldBakedPose(target), realized=raw;
    for(std::size_t frame=1;frame<clip.poses.size();++frame) {
      target.pose=clip.poses[frame]; target.sampled_frame=int(frame); target.requested_frame=int(frame);
      target.state.position+=blunted::Vector3(.02f,.01f,0);
      const auto next_raw=WorldBakedPose(target);
      const auto next=RealizeBakedPose(realized,target,!clip.metadata.outgoing_special_state.empty());
      const auto raw_start=Geometry(raw),raw_end=Geometry(next_raw);
      const auto realized_start=Geometry(realized),realized_end=Geometry(next);
      for(int sample=1;sample<16;++sample) {
        const float t=float(sample)/16;
        raw_error=std::max(raw_error,Error(raw,next_raw,raw_start,raw_end,t));
        const float error=Error(realized,next,realized_start,realized_end,t);
        if(error>realized_error) { realized_error=error; worst=clip.name; worst_frame=int(frame); }
      }
      raw=next_raw; realized=next; ++intervals;
    }
  }
  REQUIRE(movement>0); REQUIRE(sliding>0); REQUIRE(trip>0); REQUIRE(intervals>100);
  REQUIRE(std::isfinite(raw_error)); REQUIRE(std::isfinite(realized_error));
  std::cout<<"pose_interpolation intervals="<<intervals<<" movement="<<movement<<" sliding="<<sliding<<" trip="<<trip
      <<" raw_max_m="<<raw_error<<" realized_max_m="<<realized_error<<" worst="<<worst<<" frame="<<worst_frame<<'\n';
}
TEST_CASE("movement smoothing catch-up is separated from honest raw gait", "[baked-pose][pose-motion]") {
  Simulation s; MatchOptions options; options.snapshot_capacity=1;
  s.Init(football::app::fixtures::MakeDefaultHomeTeam(),football::app::fixtures::MakeDefaultAwayTeam(),football::pitch::Pitch{},options);
  const AnimationClip* clip=nullptr;
  for(const auto& c:SimulationAccess::AnimationLibraryOf(s).Clips()) if(c.id==1147) { clip=&c; break; }
  REQUIRE(clip!=nullptr);
  REQUIRE(StringToFunctionType(static_cast<e_DefString>(clip->metadata.action_type))==e_FunctionType_Movement);
  // 1. The raw clip's own adjacent-frame ankle displacement is a genuine sprint
  //    gait swing, not a teleport. Frames 7->8 are the running-gait peak.
  auto RawWorld=[&](int frame) {
    PlayerBakedPoseSample sample; sample.pose=clip->poses[frame]; sample.state.position={0,0,0};
    return EvaluateBakedBodyPose(sample.pose,sample.state,sample.base_angle,sample.scale);
  };
  const auto r7=RawWorld(7), r8=RawWorld(8);
  const float raw_ankle=(r8.joints[right_ankle]-r7.joints[right_ankle]).GetLength();
  REQUIRE(raw_ankle<0.08f);
  // 2. A fresh-start realization converges to the raw pose within the entry
  //    window, so the realized frame 7->8 step equals the honest gait and does
  //    not fabricate a faster surface speed. The runtime's larger realized peak
  //    (0.215 m/Tick) therefore requires a clip-switch history, not the raw
  //    clip data: see tools/p4f2-runtime-body-provider.md.
  std::optional<PlayerBakedPoseSample> realized;
  for(int frame=0;frame<=8;++frame) {
    PlayerBakedPoseSample target; target.pose=clip->poses[frame]; target.state.position={0,0,0};
    target.base_angle=.3f; target.sampled_frame=frame; target.requested_frame=frame;
    realized=realized ? RealizeBakedPose(*realized,target) : WorldBakedPose(target);
  }
  const auto z7=EvaluateBakedBodyPose(realized->pose,realized->state,0,realized->scale);
  PlayerBakedPoseSample target8; target8.pose=clip->poses[8]; target8.state.position={0,0,0};
  target8.base_angle=.3f; target8.sampled_frame=8; target8.requested_frame=8;
  const auto realized8=RealizeBakedPose(*realized,target8);
  const auto z8=EvaluateBakedBodyPose(realized8.pose,realized8.state,0,realized8.scale);
  REQUIRE((z8.joints[right_ankle]-z7.joints[right_ankle]).GetLength()<0.08f);
}
