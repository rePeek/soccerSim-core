// LOCO-2.6 — base-angle coordinate continuity diagnostic.
//
// The blend kernel smooths LOCAL joint poses, but SampleBakedPose applies the
// clip's base_angle (startAngle + rotationSmuggleOffset) in world space. When a
// switch changes base_angle, the world pose can rotate instantly even though
// the local blend is continuous. This test pins that mechanism with two clips
// whose local poses are identical, so any world discontinuity is attributable
// to base_angle alone.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/animation/pose_blend.hpp"
#include "football/animation/types.hpp"
#include "football/animation/transition.hpp"
using namespace football::animation;
#include "football/animation/types.hpp"
#include "player/player_baked_body_pose.hpp"
#include "player/player_body_profile.hpp"
#include "player/player_kinematics.hpp"

using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

PlayerBodyProfile Profile() {
  PlayerDefinition d;
  d.height = 1.8f;
  d.weight_kg = 75.0f;
  return MakePlayerBodyProfile(d);
}

float WorldBodyAngleDeg(const PoseFrame& pose, float base_angle) {
  const PlayerKinematicState state;
  const auto world = EvaluateBakedBodyPose(pose, state, base_angle, Profile());
  const Vector3 rel = world.joints[BodyPart::left_shoulder] -
                      world.joints[BodyPart::body];
  return static_cast<float>(rel.GetAngle2D(Vector3(0, -1, 0))) *
         180.0f / football::math::pi;
}

}  // namespace

TEST_CASE("identical local poses differ in world angle only via base_angle") {
  const PoseFrame local{};  // bind pose: symmetric, identity rotations

  const float a0 = WorldBodyAngleDeg(local, 0.0f);
  const float a90 = WorldBodyAngleDeg(local, football::math::pi / 2.0f);

  // The shoulder-to-body world bearing rotates by the base-angle delta, even
  // though the two clips' local poses are byte-identical. This is the world
  // discontinuity the adapter must eventually smooth across a switch.
  REQUIRE(std::fabs(a90 - a0) == Catch::Approx(90.0f).margin(1e-3f));
}

TEST_CASE("local blend leaves base-angle world discontinuity unsmoothed") {
  const PoseFrame source{};   // base 0
  const PoseFrame target{};   // base 90, identical local pose

  // A 50% blend of two identical local poses is that same pose.
  const PoseFrame blended = BlendPose(source, target, 0.5f);
  const float base0 = WorldBodyAngleDeg(blended, 0.0f);
  const float base90 = WorldBodyAngleDeg(blended, football::math::pi / 2.0f);

  // The blend is smooth locally, but the world bearing still jumps by the full
  // base-angle delta: the transition state does not interpolate base_angle.
  REQUIRE(std::fabs(base90 - base0) == Catch::Approx(90.0f).margin(1e-3f));
  REQUIRE(std::fabs(base90 - base0) == Catch::Approx(90.0f).margin(1e-3f));
}

TEST_CASE("transition state smooths base angle across a switch") {
  const PoseFrame source{};  // base 0
  const PoseFrame target{};  // base 90, identical local pose

  AnimationTransitionState transition;
  transition.Begin(source, 0.0f, kDefaultMovementBlendTicks);

  const PoseFrame mid = transition.Advance(target, football::math::pi / 2.0f);
  const float a_mid = WorldBodyAngleDeg(mid, transition.blended_base_angle);
  const float a0 = WorldBodyAngleDeg(source, 0.0f);
  const float a90 = WorldBodyAngleDeg(target, football::math::pi / 2.0f);
  REQUIRE(a_mid > a0);
  REQUIRE(a_mid < a90);

  PoseFrame final = mid;
  for (int i = 1; i < kDefaultMovementBlendTicks; ++i)
    final = transition.Advance(target, football::math::pi / 2.0f);
  REQUIRE_FALSE(transition.active);
  REQUIRE(transition.blended_base_angle ==
          Catch::Approx(football::math::pi / 2.0f).margin(1e-3f));
}