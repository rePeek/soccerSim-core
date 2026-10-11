#include <sstream>
#include <catch2/catch_test_macros.hpp>
#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
using football::sim::testing::SimulationAccess;
namespace {
std::string Engine(Simulation& s) { std::ostringstream out; out<<SimulationAccess::RngOf(s).engine(); return out.str(); }
void SameShape(const football::ball::ColliderShape& actual,const football::ball::ColliderShape& expected) {
  REQUIRE(actual.index()==expected.index());
  std::visit([&](const auto& a) {
    using T=std::decay_t<decltype(a)>; const auto& b=std::get<T>(expected);
    if constexpr(std::is_same_v<T,football::ball::Capsule>) { REQUIRE(a.a==b.a); REQUIRE(a.b==b.b); REQUIRE(a.radius==b.radius); }
    else if constexpr(std::is_same_v<T,football::ball::Sphere>) { REQUIRE(a.center==b.center); REQUIRE(a.radius==b.radius); }
  },actual);
}
}
TEST_CASE("six physical bodies use the published realized pose without shadow authorization", "[baked-pipeline]") {
  for(bool reverse:{false,true}) {
    const auto home=football::app::fixtures::MakeDefaultHomeTeam(), away=football::app::fixtures::MakeDefaultAwayTeam();
    MatchOptions options; options.reverse_team_processing=reverse; options.snapshot_capacity=2;
    Simulation observed,control;
    for(auto* s:{&observed,&control}) {
      s->Init(home,away,football::model::Pitch{},options);
      REQUIRE_THROWS_AS(SimulationAccess::EnableBakedBodyProduction(*s,true),std::logic_error);
      SimulationAccess::EnablePreparedTickProduction(*s,true);
      SimulationAccess::EnableBodyPhysicsProduction(*s,true);
      SimulationAccess::EnableBakedBodyProduction(*s,true);
    }
    SimulationAccess::EnableActiveTouchShadow(observed,true);
    SimulationAccess::EnableBodyCollisionShadow(control,false);
    football::ai::DefaultAI policy(home,away,football::model::Pitch{});
    std::vector<Player*> players,peers;
    for(int team=0;team<2;++team) {
      std::vector<Player*> roster; SimulationAccess::ActiveTeamPlayersOf(observed,team,roster);
      players.insert(players.end(),roster.begin(),roster.end()); roster.clear();
      SimulationAccess::ActiveTeamPlayersOf(control,team,roster); peers.insert(peers.end(),roster.begin(),roster.end());
    }
    std::size_t checked=0,sliding=0,trip=0,impacts=0,body_touches=0;
    bool injected=false;
    for(int tick=0;tick<2400;++tick) {
      PlayerControlSet controls; policy.Update(observed.Observe(),controls);
      if(tick>=1200 && tick<1800) for(auto* actor:players) if(actor->IsActive()) {
        const auto* native=controls.Get(actor->GetID()); PlayerControl tackle=native ? *native:PlayerControl{};
        tackle.action=ControlAction::Tackle; tackle.desired_speed=7; controls.Set(actor->GetID(),tackle);
      }
      if(tick==600) { players[5]->TripMe({0,1,0},2,nullptr); peers[5]->TripMe({0,1,0},2,nullptr); }
      // Construct an approaching head strike from the actual realized source;
      // do not count this controlled scenario as spontaneous native acceptance.
      if(!injected && tick>=800 && observed.Observe().ball_in_play && !observed.CommittedBakedPoses().empty()) {
        const auto& pose=observed.CommittedBakedPoses().front(); REQUIRE(pose.has_value());
        const auto head=std::get<football::ball::Sphere>(pose->Geometry().shapes[1]);
        for(auto* s:{&observed,&control}) {
          auto ball=SimulationAccess::BallOf(*s)->state();
          ball.position=head.center+blunted::Vector3(.6f,0,0); ball.velocity={-100,0,0};
          ball.angular_velocity={0,0,0};
          SimulationAccess::BallOf(*s)->Reset(ball); SimulationAccess::SetBallRetainer(*s,nullptr);
        }
        injected=true;
      }
      observed.Step(controls); control.Step(controls);
      const auto before=Engine(observed);
      const auto poses=observed.CommittedBakedPoses();
      const auto& starts=SimulationAccess::BakedBodyStartsOf(observed);
      const auto& tape=SimulationAccess::BakedBodyTapeOf(observed);
      if(!poses.empty()) {
        REQUIRE(poses.size()==observed.SnapshotMetadata().player_ids.size());
        std::size_t cursor=0;
        for(std::size_t slot=0;slot<poses.size();++slot) if(poses[slot]) {
          auto end=*poses[slot],start=*starts[slot];
          sliding+=end.action==e_FunctionType_Sliding; trip+=end.action==e_FunctionType_Trip;
          REQUIRE(end.base_angle==0);
          if(reverse) { end.Mirror(); start.Mirror(); }
          const auto expected=BuildBakedBodyColliderMotions(start.Geometry(),end.Geometry(),std::uint32_t(slot));
          for(const auto& motion:expected) {
            REQUIRE(cursor<tape.size()); REQUIRE(tape[cursor].id==motion.id);
            SameShape(tape[cursor].start,motion.start); SameShape(tape[cursor].end,motion.end); ++cursor;
          }
        }
        REQUIRE(cursor==tape.size()); ++checked;
      }
      if(const auto& commit=SimulationAccess::CommittedBallTickOf(observed); commit)
        for(const auto& contact:commit->contacts)
          impacts+=contact.collider>=kFirstBakedBodyColliderId && contact.normal_impulse>0;
      for(const auto& touch:SimulationAccess::RuleTouchesOf(observed)) {
        if(touch.source!=football::sim::event::RuleTouchSource::BodyCCD) continue;
        REQUIRE(touch.contact.has_value()); REQUIRE(touch.contact->collider>=kFirstBakedBodyColliderId);
        const auto slot=(touch.contact->collider-kFirstBakedBodyColliderId)/kBakedBodySegmentCount;
        REQUIRE(touch.accepted.player==observed.SnapshotMetadata().player_ids.at(slot));
        REQUIRE(touch.contact->normal_impulse>0); ++body_touches;
      }
      REQUIRE(Engine(observed)==before); REQUIRE(Engine(observed)==Engine(control));
      const auto& a=SimulationAccess::BallOf(observed)->state(); const auto& b=SimulationAccess::BallOf(control)->state();
      REQUIRE(a.position==b.position); REQUIRE(a.velocity==b.velocity); REQUIRE(a.angular_velocity==b.angular_velocity);
      REQUIRE_FALSE(a.orientation!=b.orientation);
      const auto& first=observed.Snapshots().Latest()->snapshot.players;
      const auto& second=control.Snapshots().Latest()->snapshot.players; REQUIRE(first.size()==second.size());
      for(std::size_t slot=0;slot<first.size();++slot) { REQUIRE(first[slot].position==second[slot].position); REQUIRE(first[slot].animation_id==second[slot].animation_id); REQUIRE(first[slot].frame==second[slot].frame); }
    }
    REQUIRE(checked>2000); REQUIRE(sliding>0); REQUIRE(trip>0);
    REQUIRE(impacts>0); REQUIRE(body_touches>0);
    observed.ResetSituation({0,0,0}); REQUIRE(observed.CommittedBakedPoses().empty());
    SimulationAccess::EnableBodyPhysicsProduction(observed,false); REQUIRE(observed.CommittedBakedPoses().empty());
    REQUIRE(observed.Stop()); REQUIRE(observed.CommittedBakedPoses().empty());
  }
}
