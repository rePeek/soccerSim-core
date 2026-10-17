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
#include "football/animation/transition.hpp"
using namespace football::animation;
#include "player/player_baked_body_pose.hpp"
#include "player/player_body_profile.hpp"
#include "player/player_kinematics.hpp"
#include "player/player_locomotion_animation.hpp"
#include "player/locomotion_animation_strategy.hpp"

using football::geometry::Capsule;
using football::geometry::Sphere;
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

bool VectorFinite(const Vector3& v) {
  return std::isfinite(v.coords[0]) && std::isfinite(v.coords[1]) &&
         std::isfinite(v.coords[2]);
}

// A realized body is a real collision geometry: every shape is finite and has a
// positive radius. This is the check the CCD test uses instead of comparing raw
// pose numbers, so an animation prediction change cannot hide a broken collider.
bool GeometryFinite(const BakedPlayerBodyCollider& collider) {
  for (const auto& shape : collider.shapes) {
    if (const auto* capsule = std::get_if<Capsule>(&shape)) {
      if (!VectorFinite(capsule->a) || !VectorFinite(capsule->b) ||
          !(capsule->radius > 0.0f))
        return false;
    } else if (const auto* sphere = std::get_if<Sphere>(&shape)) {
      if (!VectorFinite(sphere->center) || !(sphere->radius > 0.0f)) return false;
    } else {
      return false;
    }
  }
  return true;
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

TEST_CASE("an interrupted blend realizes finite geometry between both endpoints") {
  // ANIM-2 A11.5: a mid-blend interruption must start from the real blended
  // output pose, and the realized collider must stay finite. Checking the
  // geometry (not only the raw pose) is what lets a future blended-pose forecast
  // be told apart from an accidental collision-response change.
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
  const AnimationId sprint_id = select(8.0f, Vector3(0, -1, 0));
  REQUIRE(idle_id != walk_id);
  REQUIRE(walk_id != sprint_id);

  const PoseFrame idle = library.Get(static_cast<uint32_t>(idle_id)).poses.front();
  const PoseFrame walk = library.Get(static_cast<uint32_t>(walk_id)).poses.front();
  const PoseFrame sprint = library.Get(static_cast<uint32_t>(sprint_id)).poses.front();

  // One tick into idle -> walk, then interrupt with a new transition that
  // starts from that real mid-blend output.
  AnimationTransitionState first_transition;
  first_transition.Begin(idle, 0.0f, kDefaultMovementBlendTicks);
  const PoseFrame mid = first_transition.Advance(walk, 0.0f);
  AnimationTransitionState interrupted;
  interrupted.Begin(mid, 0.0f, kDefaultMovementBlendTicks);
  const PoseFrame after = interrupted.Advance(sprint, 0.0f);

  const PlayerKinematicState state;
  const auto mid_geometry = BuildBodyCollider(mid, state, 0.0f, body);
  const auto after_geometry = BuildBodyCollider(after, state, 0.0f, body);
  CHECK(GeometryFinite(mid_geometry));
  CHECK(GeometryFinite(after_geometry));
  // The interruption does not snap to the target or stay at the old output.
  REQUIRE(GeometryDiffers(after, sprint));
  REQUIRE(GeometryDiffers(after, mid));
}
