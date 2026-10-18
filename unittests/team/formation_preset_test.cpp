#include <catch2/catch_test_macros.hpp>
#include "football/team/formation_preset.hpp"
#include <limits>

using namespace football::team;
using football::math::Vector3;

TEST_CASE("Preset canonical coordinates match tactical u/v, not legacy initial y", "[team][preset]") {
  const football::pitch::Pitch pitch(100,60);
  const FormationAnchor anchor{AnchorFrame::TeamRelative,.75f,.5f};
  const Vector3 ball(8,9,1);
  const auto home = FormationAnchorToWorld(anchor,pitch,-1,ball);
  const auto away = FormationAnchorToWorld(anchor,pitch,1,ball);
  CHECK(home == Vector3(25,15,0));
  CHECK(away == Vector3(-25,-15,0));
  CHECK(pitch.contains(home.coords[0],home.coords[1]));
  CHECK(pitch.contains(away.coords[0],away.coords[1]));
  CHECK(FormationAnchorToWorld(anchor,pitch,-1,ball,true) == Vector3(25,-15,0));
  CHECK(FormationAnchorToWorld(anchor,pitch,1,ball,true) == Vector3(-25,15,0));
  CHECK(anchor.x == .75f);
  CHECK(anchor.y == .5f);
  CHECK(ball == Vector3(8,9,1));
  for (int direction : {-1,1}) {
    CHECK(FormationAnchorToWorld({AnchorFrame::TeamRelative,0,0},pitch,direction,ball) ==
        Vector3(direction * 50,0,0));
    CHECK(FormationAnchorToWorld({AnchorFrame::TeamRelative,1,1},pitch,direction,ball) ==
        Vector3(-direction * 50,-direction * 30,0));
  }
}
TEST_CASE("Ball relative preset offsets rotate and mirror without relocating ball", "[team][preset]") {
  const football::pitch::Pitch pitch(100,60);
  const FormationAnchor anchor{AnchorFrame::BallRelative,-3,-4};
  const Vector3 home_ball(45,28,1);
  const Vector3 away_ball(-45,-28,1);
  const auto home = FormationAnchorToWorld(anchor,pitch,-1,home_ball);
  const auto away = FormationAnchorToWorld(anchor,pitch,1,away_ball);
  CHECK(home == Vector3(42,24,0));
  CHECK(away == Vector3(-42,-24,0));
  CHECK(pitch.contains(home.coords[0],home.coords[1]));
  CHECK(pitch.contains(away.coords[0],away.coords[1]));
  // Half-time reverses the interpretation in the same caller frame. It does not
  // mutate the template or the supplied physical ball position.
  CHECK(FormationAnchorToWorld(anchor,pitch,1,home_ball) == Vector3(48,32,0));
  CHECK(FormationAnchorToWorld(anchor,pitch,-1,home_ball,true) == Vector3(42,32,0));
  CHECK(home_ball == Vector3(45,28,1));
  CHECK(anchor.x == -3);
  CHECK(anchor.y == -4);
  // Candidates can lie beyond the touchline: rule/spacing legalization is a
  // planner responsibility, not a hidden clamp or mutation of reusable data.
  CHECK_FALSE(pitch.contains(48,32));
}
TEST_CASE("Preset conversion rejects malformed inputs and overflow", "[team][preset]") {
  const football::pitch::Pitch pitch;
  const Vector3 zero(0);
  const float inf = std::numeric_limits<float>::infinity();
  const float max = std::numeric_limits<float>::max();
  CHECK_THROWS_AS(FormationAnchorToWorld({},pitch,0,zero),std::invalid_argument);
  CHECK_THROWS_AS(FormationAnchorToWorld({AnchorFrame::TeamRelative,-.1f,0},pitch,-1,zero),std::invalid_argument);
  CHECK_THROWS_AS(FormationAnchorToWorld({AnchorFrame::TeamRelative,.5f,1.1f},pitch,-1,zero),std::invalid_argument);
  CHECK_THROWS_AS(FormationAnchorToWorld({AnchorFrame::BallRelative,inf,0},pitch,-1,zero),std::invalid_argument);
  CHECK_THROWS_AS(FormationAnchorToWorld({AnchorFrame::BallRelative,1,2},pitch,-1,{inf,0,0}),std::invalid_argument);
  CHECK_THROWS_AS(FormationAnchorToWorld({AnchorFrame::BallRelative,max,0},pitch,-1,{max,0,0}),std::invalid_argument);
}
TEST_CASE("Preset library lookup is optional and separates attacking from defending templates", "[team][preset]") {
  FormationPresets library;
  CHECK_NOTHROW(ValidateFormationPresets(library));
  CHECK_FALSE(FindFormationPreset(library,"missing"));
  library.presets = {
    {"free_kick_attack_short",FormationPresetKind::FreeKick,FormationPresetSide::Attacking,
        {{9,{AnchorFrame::BallRelative,5,2},"receiver"}}},
    {"free_kick_defend_wall",FormationPresetKind::FreeKick,FormationPresetSide::Defending,
        {{3,{AnchorFrame::BallRelative,-10,0},"wall"}}}};
  REQUIRE_NOTHROW(ValidateFormationPresets(library));
  const auto* attack = FindFormationPreset(library,"free_kick_attack_short");
  const auto* defend = FindFormationPreset(library,"free_kick_defend_wall");
  REQUIRE(attack);
  REQUIRE(defend);
  CHECK(attack->side != defend->side);
  CHECK(attack->positions.front().slot == 9);
  CHECK(defend->positions.front().responsibility == "wall");
  CHECK_FALSE(FindFormationPreset(library,"unknown"));
}
