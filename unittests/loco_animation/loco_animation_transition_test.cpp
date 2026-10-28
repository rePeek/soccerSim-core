// ANIM-1 A2 — pure action-transition coverage on the real baked library.
//
// Drives Walk -> Sprint -> Shoot -> Trip through the pure transition state +
// pose sampling/blending, with NO Simulation, Humanoid, Ball or RNG. Verifies
// pose continuity across switches and that a clip's touch frame fires exactly
// once during its playback.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "football/animation/library.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/animation/transition.hpp"
using namespace football::animation;
#include "player/execution/locomotion_animation_strategy.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/player_locomotion_animation.hpp"

using football::math::Vector3;

namespace {

PlayerKinematicState State(float speed) {
  PlayerKinematicState s;
  s.velocity = Vector3(0.0f, -1.0f, 0.0f) * speed;
  s.bodyFacing = Vector3(0.0f, -1.0f, 0.0f);
  return s;
}

AnimationId LocomotionClip(const AnimationLibrary& lib, float speed) {
  const auto s = State(speed);
  LocomotionAnimationInput in;
  in.current = s;
  in.next = s;
  in.desired_velocity = s.velocity;
  in.body = football::player::PlayerBodyProfile{};
  return SelectLocomotionAnimation(in, lib.Clips()).animation_id;
}

const AnimationClip* FindClip(const AnimationLibrary& lib, int32_t action_type) {
  for (const AnimationClip& clip : lib.Clips()) {
    if (clip.metadata.action_type == action_type && !clip.poses.empty()) return &clip;
  }
  return nullptr;
}

float MaxJointJump(const PoseFrame& a, const PoseFrame& b) {
  float jump = 0.0f;
  for (std::size_t j = 0; j < kBodyPartCount; ++j) {
    jump = std::max(
        jump, static_cast<float>(a.orientations[j].GetRotationAngle(b.orientations[j])));
  }
  return jump;
}

// Minimal pure playback: one clip cursor + a transition, exactly the
// Player-side semantics without any Humanoid.
struct Playback {
  AnimationTransitionState transition;
  AnimationId clip = -1;
  int frame = 0;
  PoseFrame final_pose;
  bool have = false;

  const PoseFrame& Tick(const AnimationClip& target, int blend_ticks) {
    if (target.id != clip) {
      if (have) transition.Begin(final_pose, 0.0f, blend_ticks);
      else transition.Reset();
      clip = target.id;
      frame = 0;
    }
    const PoseFrame sample = SamplePose(target, static_cast<float>(frame));
    final_pose = have ? transition.Advance(sample, 0.0f) : sample;
    have = true;
    if (frame < static_cast<int>(target.poses.size()) - 1) ++frame;
    return final_pose;
  }
};

}  // namespace

TEST_CASE("Walk -> Sprint -> Shoot -> Trip stays pose-continuous") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  const AnimationId walk_id = LocomotionClip(library, 5.0f);
  const AnimationId sprint_id = LocomotionClip(library, 8.0f);
  const AnimationClip* shoot = FindClip(library, e_DefString_Shot);
  const AnimationClip* trip = FindClip(library, e_DefString_Trip);
  REQUIRE(walk_id >= 0);
  REQUIRE(sprint_id >= 0);
  REQUIRE(shoot != nullptr);
  REQUIRE(trip != nullptr);

  const AnimationClip& walk = library.Get(static_cast<uint32_t>(walk_id));
  const AnimationClip& sprint = library.Get(static_cast<uint32_t>(sprint_id));

  Playback playback;
  float max_switch_jump = 0.0f;  // pose jump at a clip boundary
  float max_frame_jump = 0.0f;   // inherent per-frame delta within a clip
  PoseFrame previous;
  bool measured = false;
  auto drive = [&](const AnimationClip& clip, int ticks, int blend_ticks) {
    for (int i = 0; i < ticks; ++i) {
      const bool switching = playback.clip != clip.id;
      const PoseFrame& frame = playback.Tick(clip, blend_ticks);
      if (measured) {
        const float jump = MaxJointJump(previous, frame);
        if (switching) max_switch_jump = std::max(max_switch_jump, jump);
        else max_frame_jump = std::max(max_frame_jump, jump);
      }
      previous = frame;
      measured = true;
    }
  };

  drive(walk, 16, kDefaultMovementBlendTicks);
  drive(sprint, 16, kDefaultMovementBlendTicks);  // Walk -> Sprint
  drive(*shoot, 16, kActionBlendTicks);           // Sprint -> Shoot
  drive(*trip, 16, kActionBlendTicks);            // Shoot -> Trip (interrupt)

  std::fprintf(stderr, "[anim] A2 switch_jump=%.3f frame_jump=%.3f\n",
               max_switch_jump, max_frame_jump);
  // The blend smooths the boundary: the first switch step is a small slice of
  // the pose delta. Within-clip frame deltas belong to the clip's own speed.
  CHECK(max_switch_jump < 0.8f);
  CHECK(std::isfinite(max_frame_jump));
}

TEST_CASE("a clip's touch frame fires exactly once during playback") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  const AnimationClip* shoot = FindClip(library, e_DefString_Shot);
  REQUIRE(shoot != nullptr);
  REQUIRE(shoot->metadata.touch_frame >= 0);

  Playback playback;
  int touches = 0;
  for (int i = 0; i < static_cast<int>(shoot->poses.size()); ++i) {
    const int before = playback.frame;
    playback.Tick(*shoot, kActionBlendTicks);
    if (before == shoot->metadata.touch_frame) ++touches;
  }
  CHECK(touches == 1);
}
