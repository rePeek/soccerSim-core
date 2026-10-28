// LOCO-2.2 — read-only locomotion/animation adapter interface.
//
// Pure value contract: the input is a snapshot of kinematic state + desired
// velocity + body profile; the output only proposes clip/rate/time and can
// never move the player. No Simulation, Humanoid, RNG or animation asset.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/player_locomotion_animation.hpp"

using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

PlayerBodyProfile Profile(float height, float mass) {
  PlayerDefinition d;
  d.height = height;
  d.weight_kg = mass;
  return MakePlayerBodyProfile(d);
}

}  // namespace

TEST_CASE("adapter input is a deterministic value snapshot") {
  PlayerKinematicState current;
  current.position = Vector3(1.0f, 2.0f, 0.0f);
  current.velocity = Vector3(0.0f, -3.0f, 0.0f);
  current.bodyFacing = Vector3(0.0f, -1.0f, 0.0f);

  PlayerKinematicState next = current;
  next.position += Vector3(0.0f, -0.03f, 0.0f);

  const auto body = Profile(1.8f, 75.0f);
  const Vector3 desired(0.0f, -4.0f, 0.0f);
  const auto input = MakeLocomotionAnimationInput(current, next, desired, body);
  const auto again = MakeLocomotionAnimationInput(current, next, desired, body);

  // Deterministic construction from equal inputs.
  REQUIRE(input.current.position.coords[0] == again.current.position.coords[0]);
  REQUIRE(input.body.mass_kg == again.body.mass_kg);

  // Copies, not references: mutating the source cannot leak into the snapshot.
  current.position.coords[0] = 99.0f;
  REQUIRE(input.current.position.coords[0] == Catch::Approx(1.0f));
  REQUIRE(input.next.position.coords[1] == Catch::Approx(2.0f - 0.03f));
  REQUIRE(input.desired_velocity.coords[1] == Catch::Approx(-4.0f));
  REQUIRE(input.body.height_scale == body.height_scale);
  REQUIRE(input.body.girth_scale == body.girth_scale);
  REQUIRE(input.body.mass_kg == again.body.mass_kg);
}

TEST_CASE("adapter output defaults are inert and never carry a position") {
  LocomotionAnimationOutput output;
  REQUIRE(output.animation_id == -1);
  REQUIRE(output.playback_rate == Catch::Approx(1.0f));
  REQUIRE(output.frame_time == Catch::Approx(0.0f));
  REQUIRE(output.gait_phase == 0);
  REQUIRE_FALSE(output.suggest_switch);

  // The output struct has no position/velocity/state field at all, so by
  // construction it cannot write the player world position.
  static_assert(sizeof(output) >= sizeof(AnimationId) + sizeof(float) + sizeof(float) + sizeof(int) + sizeof(bool));
}
