// LOCO-2.4/2.5/2.7 — headless off-ball animation acceptance.
//
// Loads the baked asset, drives SelectLocomotionAnimation over the real Motion
// library for idle / start / cruise / brake / 90 deg / 180 deg scenarios, and
// verifies that a PlayerBodyProfile changes body geometry but never changes the
// animation choice (shared library, capability-independent). No Simulation, no
// Humanoid, no RNG.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "football/animation/library.hpp"
#include "player/execution/player_baked_body_pose.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/player_locomotion_animation.hpp"
#include "player/execution/locomotion_animation_strategy.hpp"

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

PlayerKinematicState State(float speed, const Vector3& dir) {
  PlayerKinematicState s;
  s.velocity = dir.GetNormalized(Vector3(0, -1, 0)) * speed;
  s.bodyFacing = dir.GetNormalized(Vector3(0, -1, 0));
  return s;
}

LocomotionAnimationInput Input(const PlayerKinematicState& current,
                               const PlayerKinematicState& next,
                               const Vector3& desired,
                               const PlayerBodyProfile& body) {
  LocomotionAnimationInput in;
  in.current = current;
  in.next = next;
  in.desired_velocity = desired;
  in.body = body;
  return in;
}

void Dump(const char* scenario, const LocomotionAnimationChoice& choice) {
  if (std::getenv("FOOTBALL_LOCO_ACCEPTANCE_DIAG") == nullptr) return;
  std::fprintf(stderr, "scenario=%s clip=%d ref=%.2f err=%.3f rate=%.3f\n",
               scenario, choice.animation_id, choice.reference_speed,
               choice.speed_error, choice.playback_rate);
}

}  // namespace

TEST_CASE("real library selects the expected Motion class per scenario") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const auto body = Profile(1.80f, 75.0f);

  // Idle: incoming and outgoing velocity both idle.
  {
    auto s = State(0.0f, Vector3(0, -1, 0));
    const auto c = SelectLocomotionAnimation(Input(s, s, Vector3(0, 0, 0), body), library.Clips());
    Dump("idle", c);
    REQUIRE(c.animation_id >= 0);
    const auto& clip = library.Get(static_cast<uint32_t>(c.animation_id));
    CHECK(FloatToEnumVelocity(clip.metadata.incoming_velocity) == e_Velocity_Idle);
    CHECK(FloatToEnumVelocity(clip.metadata.outgoing_velocity) == e_Velocity_Idle);
  }

  // Start: accelerating out of idle toward dribble.
  {
    auto cur = State(0.1f, Vector3(0, -1, 0));
    auto next = State(0.2f, Vector3(0, -1, 0));
    const auto c = SelectLocomotionAnimation(Input(cur, next, Vector3(0, -3.5f, 0), body), library.Clips());
    Dump("start", c);
    REQUIRE(c.animation_id >= 0);
    const auto& clip = library.Get(static_cast<uint32_t>(c.animation_id));
    CHECK(clip.metadata.outgoing_velocity > clip.metadata.incoming_velocity);
  }

  // Cruise at walk speed: steady clip with reference speed near 5.
  {
    auto s = State(5.0f, Vector3(0, -1, 0));
    const auto c = SelectLocomotionAnimation(Input(s, s, Vector3(0, -5.0f, 0), body), library.Clips());
    Dump("cruise5", c);
    REQUIRE(c.animation_id >= 0);
    const auto& clip = library.Get(static_cast<uint32_t>(c.animation_id));
    CHECK(std::fabs(clip.metadata.outgoing_velocity - clip.metadata.incoming_velocity) < 0.3f);
    CHECK(std::fabs(c.reference_speed - 5.0f) < 1.5f);
  }

  // Brake: decelerating to idle.
  {
    auto cur = State(5.0f, Vector3(0, -1, 0));
    auto next = State(4.8f, Vector3(0, -1, 0));
    const auto c = SelectLocomotionAnimation(Input(cur, next, Vector3(0, 0, 0), body), library.Clips());
    Dump("brake", c);
    REQUIRE(c.animation_id >= 0);
    const auto& clip = library.Get(static_cast<uint32_t>(c.animation_id));
    CHECK(clip.metadata.outgoing_velocity < clip.metadata.incoming_velocity);
  }

  // 90 degree turn while cruising.
  {
    const auto dir = Vector3(0, -1, 0).GetRotated2D(90.0f * football::math::pi / 180.0f);
    auto cur = State(3.5f, Vector3(0, -1, 0));
    auto next = State(3.5f, dir);
    const auto c = SelectLocomotionAnimation(Input(cur, next, dir * 3.5f, body), library.Clips());
    Dump("turn90", c);
    REQUIRE(c.animation_id >= 0);
    const auto& clip = library.Get(static_cast<uint32_t>(c.animation_id));
    const float turn = locomotion_strategy_detail::ClipTurnDeg(clip);
    CHECK(turn > 60.0f);
    CHECK(turn < 120.0f);
  }

  // 180 degree turnaround.
  {
    auto cur = State(3.5f, Vector3(0, -1, 0));
    auto next = State(3.5f, Vector3(0, 1, 0));
    const auto c = SelectLocomotionAnimation(Input(cur, next, Vector3(0, 3.5f, 0), body), library.Clips());
    Dump("turn180", c);
    REQUIRE(c.animation_id >= 0);
    const auto& clip = library.Get(static_cast<uint32_t>(c.animation_id));
    CHECK(locomotion_strategy_detail::ClipTurnDeg(clip) > 150.0f);
  }
}

TEST_CASE("selection is deterministic on the real library") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const auto body = Profile(1.80f, 75.0f);
  auto s = State(5.0f, Vector3(0, -1, 0));
  const auto a = SelectLocomotionAnimation(Input(s, s, Vector3(0, -5.0f, 0), body), library.Clips());
  const auto b = SelectLocomotionAnimation(Input(s, s, Vector3(0, -5.0f, 0), body), library.Clips());
  CHECK(a.animation_id == b.animation_id);
  CHECK(a.playback_rate == b.playback_rate);
}

TEST_CASE("body profile changes geometry but never the animation choice") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  const auto slim = Profile(1.65f, 53.0f);
  const auto broad = Profile(1.95f, 105.0f);
  auto s = State(3.5f, Vector3(0, -1, 0));

  const auto a = SelectLocomotionAnimation(Input(s, s, Vector3(0, -3.5f, 0), slim), library.Clips());
  const auto b = SelectLocomotionAnimation(Input(s, s, Vector3(0, -3.5f, 0), broad), library.Clips());
  REQUIRE(a.animation_id == b.animation_id);

  // The same animation is shared; only the body geometry differs.
  const PoseFrame standing{};
  PlayerKinematicState at_origin;
  const auto pose_slim = EvaluateBakedBodyPose(standing, at_origin, 0.0f, slim);
  const auto pose_broad = EvaluateBakedBodyPose(standing, at_origin, 0.0f, broad);
  REQUIRE(pose_broad.joints[body].coords[2] > pose_slim.joints[body].coords[2]);
  REQUIRE(pose_broad.joints[neck].coords[2] > pose_slim.joints[neck].coords[2]);
  REQUIRE(pose_broad.joints[left_knee].coords[2] > pose_slim.joints[left_knee].coords[2]);
}