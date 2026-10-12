#include <array>
#include "football/pitch/pitch.hpp"
#include <iostream>
#include <sstream>
#include <catch2/catch_test_macros.hpp>
#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/player/player_baked_pose_realization.hpp"

using football::sim::testing::SimulationAccess;
namespace {
float JointDelta(const PlayerBakedPoseSample& a, const PlayerBakedPoseSample& b) {
  const auto first = EvaluateBakedBodyPose(a.pose,a.state,a.base_angle,a.scale);
  const auto second = EvaluateBakedBodyPose(b.pose,b.state,b.base_angle,b.scale);
  float max = 0;
  // Root translation is measured separately; this diagnostic isolates relative
  // joint changes and does not call discontinuous pose changes 'body velocity'.
  for (std::size_t j=0;j<kBodyPartCount;++j)
    max = std::max(max,((second.joints[j]-b.state.position)-(first.joints[j]-a.state.position)).GetLength());
  return max;
}
std::string RngState(Simulation& s) { std::ostringstream out; out << SimulationAccess::RngOf(s).engine(); return out.str(); }
}
TEST_CASE("runtime baked samples observe real frames without another actor pass", "[baked-pose][pose-runtime]") {
  for (bool reverse : {false,true}) {
    const auto home=football::app::fixtures::MakeDefaultHomeTeam(), away=football::app::fixtures::MakeDefaultAwayTeam();
    MatchOptions options; options.snapshot_capacity=2; options.reverse_team_processing=reverse;
    Simulation observed, control;
    observed.Init(home,away,football::pitch::Pitch{},options);
    control.Init(home,away,football::pitch::Pitch{},options);
    SimulationAccess::EnablePreparedTickProduction(observed,true); SimulationAccess::EnablePreparedTickProduction(control,true);
    SimulationAccess::EnableBodyPhysicsProduction(observed,true); SimulationAccess::EnableBodyPhysicsProduction(control,true);
    football::ai::DefaultAI policy(home,away,football::pitch::Pitch{});
    std::vector<Player*> players, peers;
    for(int team=0;team<2;++team) {
      std::vector<Player*> roster;
      SimulationAccess::ActiveTeamPlayersOf(observed,team,roster); players.insert(players.end(),roster.begin(),roster.end());
      roster.clear(); SimulationAccess::ActiveTeamPlayersOf(control,team,roster); peers.insert(peers.end(),roster.begin(),roster.end());
    }
    std::size_t switches=0, sliding=0, trip=0, clamped=0;
    float same_clip_jump=0, switched_clip_jump=0, root_rotation_component=0;
    std::string worst_switch;
    std::vector<std::optional<PlayerBakedPoseSample>> realized(players.size());
    std::optional<std::uint64_t> realized_generation;
    float realized_switch_delta=0, realized_delta=0;
    std::size_t running_pose_ticks=0,running_leg_samples=0;
    std::string worst_realized;
    for(int tick=0;tick<2400;++tick) {
      std::vector<std::optional<PlayerBakedPoseSample>> before;
      for(auto* actor:players) before.push_back(actor->IsActive() ?
          std::optional{actor->GetBakedPoseSample(actor->GetModel().height/defaultPlayerHeight)} : std::nullopt);
      const auto* prior_record=observed.Snapshots().Latest();
      const auto prior_generation=prior_record ? std::optional{prior_record->stamp.generation} : std::nullopt;
      PlayerControlSet controls; policy.Update(observed.Observe(),controls);
      if(tick>=1200 && tick<1800) {
        for(auto* actor:players) {
          if(!actor->IsActive()) continue;
          const auto* native=controls.Get(actor->GetID());
          PlayerControl tackle=native ? *native : PlayerControl{};
          tackle.action=ControlAction::Tackle; tackle.desired_speed=7;
          controls.Set(actor->GetID(),tackle);
        }
      }
      if(tick==600) { players[5]->TripMe({0,1,0},2,nullptr); peers[5]->TripMe({0,1,0},2,nullptr); }
      observed.Step(controls); control.Step(controls);
      const auto rng_before=RngState(observed);
      const auto& library=SimulationAccess::AnimationLibraryOf(observed);
      const auto generation=observed.Snapshots().Latest()->stamp.generation;
      if(!realized_generation || *realized_generation!=generation) {
        for(auto& pose:realized) pose.reset();
        realized_generation=generation;
      }
      for(std::size_t slot=0;slot<players.size();++slot) {
        auto& actor=*players[slot]; if(!actor.IsActive()) continue;
        const auto sample=actor.GetBakedPoseSample(actor.GetModel().height/defaultPlayerHeight);
        const auto repeat=actor.GetBakedPoseSample(actor.GetModel().height/defaultPlayerHeight);
        REQUIRE(sample.animation_id==players[slot]->GetCurrentAnim()->animationId);
        const auto& source=library.Get(sample.animation_id).poses.at(sample.sampled_frame);
        REQUIRE(sample.requested_frame==players[slot]->GetFrameNum());
        REQUIRE(sample.state.position==actor.GetKinematicState().position);
        for(int axis=0;axis<3;++axis) root_rotation_component=std::max(root_rotation_component,
            std::fabs(sample.pose.orientations[BodyPart::player].elements[axis]));
        REQUIRE(sample.base_angle==repeat.base_angle);
        for(std::size_t joint=0;joint<kBodyPartCount;++joint) {
          REQUIRE_FALSE(sample.pose.orientations[joint]!=repeat.pose.orientations[joint]);
          REQUIRE(sample.pose.positions[joint]==repeat.pose.positions[joint]);
          REQUIRE_FALSE(sample.pose.orientations[joint]!=source.orientations[joint]);
          REQUIRE(sample.pose.positions[joint]==source.positions[joint]);
        }
        const auto prior_realized=realized[slot];
        realized[slot]=prior_realized ? RealizeBakedPose(*prior_realized,sample,
            !library.Get(sample.animation_id).metadata.outgoing_special_state.empty()) : WorldBakedPose(sample);
        if(prior_realized) {
          const float difference=JointDelta(*prior_realized,*realized[slot]);
          if(difference>realized_delta) {
            realized_delta=difference; std::ostringstream context;
            context<<"tick="<<tick<<" player="<<actor.GetID()<<" clips="<<prior_realized->animation_id<<':'<<sample.animation_id
                <<" frames="<<prior_realized->sampled_frame<<':'<<sample.sampled_frame
                <<" actions="<<prior_realized->action<<':'<<sample.action; worst_realized=context.str();
          }
          if(sample.animation_id!=prior_realized->animation_id || sample.sampled_frame<prior_realized->sampled_frame)
            realized_switch_delta=std::max(realized_switch_delta,difference);
          // FK bone lengths hold after actual animation realization, not a
          // separate collider-only pose clamp or collision exclusion.
          const auto world=EvaluateBakedBodyPose(realized[slot]->pose,realized[slot]->state,0,sample.scale);
          REQUIRE(std::fabs((world.joints[left_knee]-world.joints[left_thigh]).GetLength()-.42f*sample.scale)<1e-5f);
          REQUIRE(std::fabs((world.joints[right_knee]-world.joints[right_thigh]).GetLength()-.42f*sample.scale)<1e-5f);
        }
        const auto geometry=sample.Geometry();
        auto reflected=sample; reflected.Mirror(); const auto reflected_geometry=reflected.Geometry();
        for(std::size_t part=0;part<geometry.shapes.size();++part) {
          std::visit([&](const auto& primitive) {
            using T=std::decay_t<decltype(primitive)>;
            const auto& mirror=std::get<T>(reflected_geometry.shapes[part]);
            if constexpr(std::is_same_v<T,football::ball::Capsule>) {
              auto a=primitive.a,b=primitive.b; a.Mirror(); b.Mirror();
              REQUIRE((a-mirror.a).GetLength()<1e-5f); REQUIRE((b-mirror.b).GetLength()<1e-5f);
            } else if constexpr(std::is_same_v<T,football::ball::Sphere>) {
              auto center=primitive.center; center.Mirror(); REQUIRE((center-mirror.center).GetLength()<1e-5f);
            }
          },geometry.shapes[part]);
        }
        clamped+=sample.frame_clamped;
        sliding+=sample.action==e_FunctionType_Sliding; trip+=sample.action==e_FunctionType_Trip;
        // A rule reset/end change is a placement boundary, not a swept pose.
        if(before[slot] && prior_generation && *prior_generation==observed.Snapshots().Latest()->stamp.generation) {
          if(sample.action==e_FunctionType_Movement && sample.state.velocity.GetLength()>3.f) {
            ++running_pose_ticks;
            const auto action=StringToFunctionType(static_cast<e_DefString>(library.Get(sample.animation_id).metadata.action_type));
            REQUIRE(action==e_FunctionType_Movement);
            if(sample.animation_id==before[slot]->animation_id && sample.sampled_frame==before[slot]->sampled_frame+1 &&
                (sample.pose.orientations[left_thigh]!=before[slot]->pose.orientations[left_thigh] ||
                 sample.pose.orientations[right_thigh]!=before[slot]->pose.orientations[right_thigh])) ++running_leg_samples;
          }
          const auto motions=BuildBakedBodyColliderMotions(before[slot]->Geometry(),geometry,std::uint32_t(slot));
          for(std::size_t segment=0;segment<motions.size();++segment)
            REQUIRE(motions[segment].id==BakedBodyColliderId(std::uint32_t(slot),static_cast<BakedBodySegment>(segment)));
          const float delta=JointDelta(*before[slot],sample);
          if(sample.animation_id!=before[slot]->animation_id || sample.sampled_frame<before[slot]->sampled_frame) {
            ++switches;
            if(delta>switched_clip_jump) {
              switched_clip_jump=delta; std::ostringstream context;
              context << "tick=" << tick << " player=" << actor.GetID()
                  << " clips=" << before[slot]->animation_id << ':' << sample.animation_id
                  << " frames=" << before[slot]->sampled_frame << ':' << sample.sampled_frame
                  << " base_angles=" << before[slot]->base_angle << ':' << sample.base_angle;
              worst_switch=context.str();
            }
          }
          else same_clip_jump=std::max(same_clip_jump,delta);
        }
      }
      REQUIRE(RngState(observed)==rng_before); REQUIRE(RngState(observed)==RngState(control));
      const auto& ball=SimulationAccess::BallOf(observed)->state(); const auto& other=SimulationAccess::BallOf(control)->state();
      REQUIRE(ball.position==other.position); REQUIRE(ball.velocity==other.velocity); REQUIRE(ball.angular_velocity==other.angular_velocity);
      REQUIRE_FALSE(ball.orientation!=other.orientation);
      const auto& a=observed.Snapshots().Latest()->snapshot.players; const auto& b=control.Snapshots().Latest()->snapshot.players;
      REQUIRE(a.size()==b.size());
      for(std::size_t i=0;i<a.size();++i) { REQUIRE(a[i].position==b[i].position); REQUIRE(a[i].frame==b[i].frame); REQUIRE(a[i].animation_id==b[i].animation_id); }
    }
    std::cout<<"pose_runtime reverse="<<reverse<<" switches="<<switches<<" sliding="<<sliding<<" trip="<<trip
             <<" clamped="<<clamped<<" same_clip_joint_delta_max="<<same_clip_jump
             <<" switched_clip_joint_delta_max="<<switched_clip_jump
             <<" root_rotation_component_max="<<root_rotation_component<<" worst_switch="<<worst_switch
             <<" realized_delta_max="<<realized_delta<<" realized_switch_delta_max="<<realized_switch_delta
             <<" running_pose_ticks="<<running_pose_ticks<<" running_leg_samples="<<running_leg_samples
             <<" worst_realized="<<worst_realized<<'\n';
    REQUIRE(switches>0); REQUIRE(sliding>0); REQUIRE(trip>0); REQUIRE(clamped==0);
    REQUIRE(root_rotation_component==0);
    REQUIRE(realized_switch_delta<switched_clip_jump);
    REQUIRE(running_pose_ticks>0); REQUIRE(running_leg_samples>0);
  }
}
