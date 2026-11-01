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
#include <limits>
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
  d.weight = mass;
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
// ---------------------------------------------------------------------------
// STRIDE-0: baseline calibration of the real Movement catalog.
//
// The playback rate is derived from `LocomotionClipReferenceSpeed`, so that
// number has to agree with the clip's own baked velocity metadata. If it did
// not, every locomotion clip would be played systematically too fast or too
// slow and no amount of stride adaptation could hide it.
// ---------------------------------------------------------------------------
TEST_CASE("reference speed matches the clip's own root track") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  int checked = 0;
  int constant_speed = 0;
  float worst_constant_error = 0.0f;
  int worst_constant_clip = -1;
  int worst_clip = -1;
  float worst_excess = 0.0f;
  for (const auto& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;
    const auto& roots = clip.root_positions;
    if (roots.size() < 4 || clip.frame_count < 4) continue;

    // Mean speed over the whole clip, from the same samples the bake used.
    const float measured = LocomotionClipReferenceSpeed(clip);
    // Endpoint estimates from the clip's own first and last three intervals.
    const float entering = (roots[3] - roots[0]).Get2D().GetLength() / 0.03f;
    const float leaving = (roots[roots.size() - 1] - roots[roots.size() - 4])
                              .Get2D()
                              .GetLength() /
                          0.03f;
    const float low = std::min(entering, leaving);
    const float high = std::max(entering, leaving);
    if (high < 1.0f) continue;  // idle clips carry no usable reference speed

    // A clip whose root track is uniform must have a matching mean. This is the
    // case an N-versus-N-1 span bug breaks, by N/(N-1) = 4% at 25 frames, on
    // every single clip.
    float slowest = std::numeric_limits<float>::infinity();
    float fastest = 0.0f;
    for (std::size_t frame = 1; frame < roots.size(); ++frame) {
      const float speed =
          (roots[frame] - roots[frame - 1]).Get2D().GetLength() / 0.01f;
      slowest = std::min(slowest, speed);
      fastest = std::max(fastest, speed);
    }
    const bool uniform_track =
        fastest > 1e-3f && (fastest - slowest) / fastest < 0.02f;
    if (uniform_track) {
      const float error = std::fabs(measured - high) / high;
      if (std::getenv("FOOTBALL_LOCO_ACCEPTANCE_DIAG") != nullptr) {
        std::fprintf(stderr, "const clip=%d ref=%.3f track=%.3f err=%.4f\n",
                     static_cast<int>(clip.id), measured, high, error);
      }
      if (error > worst_constant_error) {
        worst_constant_error = error;
        worst_constant_clip = static_cast<int>(clip.id);
      }
      ++constant_speed;
    }

    // Every other clip may legitimately change speed inside itself, so only a
    // loose bound applies: the mean must not sit far outside its own ends.
    const float excess = std::max(measured - high, low - measured) / high;
    if (std::getenv("FOOTBALL_LOCO_ACCEPTANCE_DIAG") != nullptr) {
      std::fprintf(stderr, "clip=%d ref=%.3f enter=%.3f leave=%.3f excess=%.3f\n",
                   static_cast<int>(clip.id), measured, entering, leaving, excess);
    }
    if (excess > worst_excess) {
      worst_excess = excess;
      worst_clip = static_cast<int>(clip.id);
    }
    ++checked;
  }

  REQUIRE(checked > 100);
  REQUIRE(constant_speed > 20);
  INFO("constant-speed clip " << worst_constant_clip << " error "
                              << worst_constant_error);
  CHECK(worst_constant_error < 0.02f);
  // Clip 1180 is a single-step clip whose root track is stationary at both ends;
  // its mean is therefore well above its endpoints. Everything else stays close.
  INFO("worst clip " << worst_clip << " excess " << worst_excess);
  CHECK(worst_excess < 1.0f);
}

TEST_CASE("motion-driven playback shortens the cadence instead of the step") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const auto body = Profile(1.80f, 75.0f);
  auto s = State(5.0f, Vector3(0, -1, 0));
  const auto c = SelectLocomotionAnimation(
      Input(s, s, Vector3(0, -5.0f, 0), body), library.Clips());
  REQUIRE(c.animation_id >= 0);
  const auto& clip = library.Get(static_cast<uint32_t>(c.animation_id));
  const float reference = LocomotionClipReferenceSpeed(clip);
  REQUIRE(reference > 1.0f);

  // Slower than the clip: the stride is shortened and the cadence is derived
  // from the same scale, so the support foot still tracks the root.
  const float achieved = 0.6f * reference;
  const float stride = LocomotionStrideScaleTarget(achieved, reference);
  LocomotionPlaybackRequest request;
  for (int tick = 0; tick < 5; ++tick) {
    request = AdvanceLocomotionPlayback(request, clip.id, clip, achieved,
                                       LocomotionClipGaitSpeed(clip), 0.0f,
                                        0.01f, stride);
  }
  CHECK(request.playback_rate > 0.0f);
  CHECK(request.playback_rate <= kLocomotionMaxPlaybackRate);
  CHECK(request.frame_time >= 0.0f);
  CHECK(request.frame_time < static_cast<float>(clip.frame_count) - 1.0f);
  // The pairing uses the speed the *feet* imply, which on authored clips is not
  // the root track's speed (the animation has foot slide baked in).
  const float gait_speed = LocomotionClipGaitSpeed(clip);
  CHECK(gait_speed > 0.5f);
  const float world_foot_travel = stride * gait_speed * request.playback_rate;
  CHECK(world_foot_travel == Catch::Approx(achieved).margin(1e-3f));
  // Stride adaptation is not a no-op here: it really shortens the step.
  CHECK(stride < 1.0f);
}
