#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "sim/player/player_baked_body_pose.hpp"
using football::sim::testing::SimulationAccess;
namespace {
std::vector<blunted::Vector3> Vertices(const char* file) {
  std::ifstream stream(std::string(FOOTBALL_TEST_DATA_DIR)+"/media/objects/players/models/"+file);
  REQUIRE(stream.good());
  std::vector<blunted::Vector3> vertices; std::string line;
  while(std::getline(stream,line)) {
    std::istringstream row(line); std::string token; row>>token;
    if(token!="*MESH_VERTEX") continue;
    int index; float x,y,z; REQUIRE(bool(row>>index>>x>>y>>z)); vertices.push_back({x,y,z});
  }
  REQUIRE_FALSE(vertices.empty()); return vertices;
}
float Gap(blunted::Vector3 vertex, const football::ball::Capsule& capsule) {
  const auto axis=capsule.b-capsule.a;
  const float s=std::clamp((vertex-capsule.a).GetDotProduct(axis)/axis.GetDotProduct(axis),0.f,1.f);
  return (vertex-capsule.a-axis*s).GetLength()-capsule.radius;
}
}
TEST_CASE("head and leg proxy envelopes cover the named baked meshes in transformed poses", "[baked-pose][mesh-envelope]") {
  const auto head=Vertices("head.ase"), thigh=Vertices("upperleg.ase"), shin=Vertices("lowerleg.ase");
  REQUIRE(head.size()==222); REQUIRE(thigh.size()==207); REQUIRE(shin.size()==224);
  Simulation s; MatchOptions options; options.snapshot_capacity=1;
  s.Init(football::app::fixtures::MakeDefaultHomeTeam(),football::app::fixtures::MakeDefaultAwayTeam(),football::model::Pitch{},options);
  const auto& library=SimulationAccess::AnimationLibraryOf(s);
  std::vector<Player*> actors; SimulationAccess::ActiveTeamPlayersOf(s,0,actors);
  const auto reference_id=actors.front()->GetBakedPoseSample(1).animation_id;
  const auto& reference=library.Get(reference_id);
  INFO("Initial reference pose: "<<reference.name<<" action="<<reference.metadata.action_type);
  std::size_t references=0, sliding=0, trip=0, movement=0;
  PlayerKinematicState state; state.position={5,2,0};
  for(const auto& clip:library.Clips()) {
    const auto action=StringToFunctionType(static_cast<e_DefString>(clip.metadata.action_type));
    if(&clip!=&reference && action!=e_FunctionType_Movement && action!=e_FunctionType_Sliding && action!=e_FunctionType_Trip) continue;
    references+=&clip==&reference;
    sliding+=action==e_FunctionType_Sliding; trip+=action==e_FunctionType_Trip;
    movement+=action==e_FunctionType_Movement;
    REQUIRE_FALSE(clip.poses.empty());
    for(const auto frame:{std::size_t(0),clip.poses.size()/2,clip.poses.size()-1}) for(float scale:{.88f,1.05f}) {
      const auto& pose=clip.poses[frame]; const auto world=EvaluateBakedBodyPose(pose,state,.37f,scale);
      const auto shapes=BuildBodyCollider(pose,state,.37f,scale);
      const auto& sphere=std::get<football::ball::Sphere>(shapes.shapes[1]);
      for(const auto& vertex:head) {
        const auto point=world.joints[neck]+world.orientations[neck]*(vertex*scale);
        REQUIRE((point-sphere.center).GetLength()<=sphere.radius+1e-5f);
      }
      for(const auto [joint,segment]:{std::pair{left_thigh,2},std::pair{right_thigh,4},std::pair{left_knee,3},std::pair{right_knee,5}}) {
        const auto& vertices=segment==2 || segment==4 ? thigh:shin;
        const auto& capsule=std::get<football::ball::Capsule>(shapes.shapes[segment]);
        for(const auto& vertex:vertices) {
          const auto point=world.joints[joint]+world.orientations[joint]*(vertex*scale);
          REQUIRE(Gap(point,capsule)<=1e-5f);
        }
      }
    }
  }
  REQUIRE(references==1); REQUIRE(sliding>0); REQUIRE(trip>0); REQUIRE(movement>0);
}
