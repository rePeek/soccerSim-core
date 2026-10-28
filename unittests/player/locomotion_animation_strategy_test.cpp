// LOCO-2.3a — pure locomotion animation selection strategy tests.
//
// Synthetic clip set only: no animation asset, no Simulation, no Humanoid, no
// RNG. Verifies idle / start-from-standstill / low+high cruise / brake / turn
// selection and determinism, all through SelectLocomotionAnimation.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/player/state/player_kinematics.hpp"
#include "player/execution/player_locomotion_animation.hpp"
#include "player/execution/locomotion_animation_strategy.hpp"

using football::math::Vector3;

namespace {

AnimationClip MakeClip(uint32_t id, float incoming, float outgoing, float turn_deg,
                       float displacement, uint32_t frames = 25) {
  AnimationClip clip;
  clip.id = id;
  clip.name = "synth" + std::to_string(id);
  clip.frame_count = frames;
  clip.root_positions = {Vector3(0, 0, 0), Vector3(0, -displacement, 0)};
  clip.metadata.action_type = e_DefString_Movement;
  clip.metadata.incoming_velocity = incoming;
  clip.metadata.outgoing_velocity = outgoing;
  clip.metadata.incoming_body_direction = Vector3(0, -1, 0);
  clip.metadata.outgoing_direction = Vector3(0, -1, 0);
  clip.metadata.outgoing_body_angle = turn_deg * football::math::pi / 180.0f;
  clip.metadata.base_animation = false;
  clip.metadata.touch_frame = -1;
  return clip;
}

std::vector<AnimationClip> SyntheticClips() {
  return {
      MakeClip(0, 0.0f, 0.0f, 0.0f, 0.00f),   // idle cruise
      MakeClip(1, 0.0f, 3.5f, 0.0f, 0.24f),   // idle -> dribble accel
      MakeClip(2, 3.5f, 3.5f, 0.0f, 0.84f),   // dribble cruise
      MakeClip(3, 5.0f, 5.0f, 0.0f, 1.25f),   // walk cruise
      MakeClip(4, 8.0f, 8.0f, 0.0f, 2.00f),   // sprint cruise
      MakeClip(5, 5.0f, 0.0f, 0.0f, 0.60f),   // walk -> idle decel
      MakeClip(6, 3.5f, 3.5f, 90.0f, 0.62f),  // dribble 90-degree turn
  };
}

PlayerKinematicState State(float speed, const Vector3& dir) {
  PlayerKinematicState s;
  s.velocity = dir.GetNormalized(Vector3(0, -1, 0)) * speed;
  s.bodyFacing = dir.GetNormalized(Vector3(0, -1, 0));
  return s;
}

LocomotionAnimationInput Input(const PlayerKinematicState& current,
                               const PlayerKinematicState& next,
                               const Vector3& desired) {
  LocomotionAnimationInput in;
  in.current = current;
  in.next = next;
  in.desired_velocity = desired;
  in.body = football::player::PlayerBodyProfile{};
  return in;
}

}  // namespace

TEST_CASE("idle selects the standing clip") {
  auto clips = SyntheticClips();
  auto current = State(0.0f, Vector3(0, -1, 0));
  auto next = current;
  const auto choice = SelectLocomotionAnimation(Input(current, next, Vector3(0, 0, 0)), clips);
  REQUIRE(choice.animation_id == 0);
}

TEST_CASE("start from standstill selects the acceleration clip") {
  auto clips = SyntheticClips();
  auto current = State(0.05f, Vector3(0, -1, 0));
  auto next = State(0.10f, Vector3(0, -1, 0));
  const auto choice = SelectLocomotionAnimation(Input(current, next, Vector3(0, -3.5f, 0)), clips);
  REQUIRE(choice.animation_id == 1);
  REQUIRE(choice.playback_rate > 0.0f);
}

TEST_CASE("low cruise selects the nearest reference speed") {
  auto clips = SyntheticClips();
  auto state = State(2.0f, Vector3(0, -1, 0));
  const auto choice = SelectLocomotionAnimation(Input(state, state, Vector3(0, -2.0f, 0)), clips);
  REQUIRE(choice.animation_id == 2);  // dribble cruise (3.36) is closest to 2.0
}

TEST_CASE("high cruise selects walk reference over sprint") {
  auto clips = SyntheticClips();
  auto state = State(6.0f, Vector3(0, -1, 0));
  const auto choice = SelectLocomotionAnimation(Input(state, state, Vector3(0, -6.0f, 0)), clips);
  REQUIRE(choice.animation_id == 3);  // walk reference 5.0 is closest to 6.0
}

TEST_CASE("decelerating to a stop selects the deceleration clip") {
  auto clips = SyntheticClips();
  auto current = State(5.0f, Vector3(0, -1, 0));
  auto next = State(4.8f, Vector3(0, -1, 0));
  const auto choice = SelectLocomotionAnimation(Input(current, next, Vector3(0, 0, 0)), clips);
  REQUIRE(choice.animation_id == 5);
}

TEST_CASE("a 90 degree turn while cruising prefers the turning clip") {
  auto clips = SyntheticClips();
  const auto dir = Vector3(0, -1, 0).GetRotated2D(90.0f * football::math::pi / 180.0f);
  auto current = State(3.5f, Vector3(0, -1, 0));
  auto next = State(3.5f, dir);
  const auto choice = SelectLocomotionAnimation(Input(current, next, dir * 3.5f), clips);
  REQUIRE(choice.animation_id == 6);
}

TEST_CASE("near-zero speed keeps a stable direction reference") {
  auto clips = SyntheticClips();
  // Sideways body facing must not produce a degenerate motion direction at
  // zero speed; selection stays deterministic and picks idle.
  auto state = State(0.0f, Vector3(1.0f, 0.0f, 0.0f));
  const auto a = SelectLocomotionAnimation(Input(state, state, Vector3(0, 0, 0)), clips);
  const auto b = SelectLocomotionAnimation(Input(state, state, Vector3(0, 0, 0)), clips);
  REQUIRE(a.animation_id == b.animation_id);
  REQUIRE(a.animation_id == 0);
}

TEST_CASE("empty clip list yields no proposal") {
  std::vector<AnimationClip> empty;
  auto state = State(2.0f, Vector3(0, -1, 0));
  const auto choice = SelectLocomotionAnimation(Input(state, state, Vector3(0, -2.0f, 0)), empty);
  REQUIRE(choice.animation_id == -1);
  REQUIRE(choice.playback_rate == Catch::Approx(1.0f));
}