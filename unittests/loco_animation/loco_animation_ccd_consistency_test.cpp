// LOCO-2.6d — forecast vs blended body-geometry consistency.
//
// The baked-body forecast in simulation_baked_body.cpp samples the raw next
// clip frame (`future.pose = clip.poses[next_frame]`), while the actual tick
// endpoint samples the blended final pose. This test pins that they genuinely
// differ during a transition, and that turning the blend ON changes the body
// geometry endpoints relative to OFF. It runs headless on the real asset: no
// Simulation, no Humanoid, no RNG.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <variant>

#include "football/animation/library.hpp"
#include "sim/player/animation_transition_state.hpp"
#include "sim/player/player_baked_body_pose.hpp"
#include "sim/player/player_body_profile.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_locomotion_animation.hpp"
#include "sim/player/locomotion_animation_strategy.hpp"

using football::geometry::Capsule;
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

PlayerKinematicState State(float speed, const Vector3& dir) {
  PlayerKinematicState s;
  s.velocity = dir.GetNormalized(Vector3(0, -1, 0)) * speed;
  s.bodyFacing = dir.GetNormalized(Vector3(0, -1, 0));
  return s;
}

bool GeometryDiffers(const PoseFrame& a, const PoseFrame& b) {
  const PlayerKinematicState state;
  const auto ga = BuildBodyCollider(a, state, 0.0f, Profile());
  const auto gb = BuildBodyCollider(b, state, 0.0f, Profile());
  for (std::size_t i = 0; i < ga.shapes.size(); ++i) {
    const auto& ca = std::get<Capsule>(ga.shapes[i]);
    const auto& cb = std::get<Capsule>(gb.shapes[i]);
    for (int c = 0; c < 3; ++c) {
      if (std::fabs(ca.a.coords[c] - cb.a.coords[c]) > 1e-4f) return true;
      if (std::fabs(ca.b.coords[c] - cb.b.coords[c]) > 1e-4f) return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("blend ON changes body geometry versus OFF during a switch") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  const auto body = Profile();
  const auto select = [&](float speed, const Vector3& dir) {
    auto s = State(speed, dir);
    LocomotionAnimationInput in;
    in.current = s;
    in.next = s;
    in.desired_velocity = dir * speed;
    in.body = body;
    return SelectLocomotionAnimation(in, library.Clips()).animation_id;
  };

  const AnimationId idle_id = select(0.0f, Vector3(0, -1, 0));
  const AnimationId walk_id = select(5.0f, Vector3(0, -1, 0));
  REQUIRE(idle_id != walk_id);

  const PoseFrame source = library.Get(static_cast<uint32_t>(idle_id)).poses.front();
  const PoseFrame target = library.Get(static_cast<uint32_t>(walk_id)).poses.front();

  // ON: the first blended tick of an idle->walk transition.
  AnimationTransitionState on;
  on.Begin(source, 0.0f, kDefaultMovementBlendTicks);
  const PoseFrame blended = on.Advance(target, 0.0f);

  // OFF / forecast: the raw next clip frame, i.e. the target's first pose.
  const PoseFrame raw = target;

  REQUIRE(GeometryDiffers(blended, raw));

  // Mid-transition the blended pose is neither the source nor the target.
  REQUIRE(GeometryDiffers(blended, source));
  REQUIRE(GeometryDiffers(blended, target));
}