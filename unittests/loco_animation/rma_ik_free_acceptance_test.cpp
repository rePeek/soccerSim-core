// Real-catalog acceptance for the IK-free locomotion path.
//
// The synthetic adapter tests prove the numeric contract. This test drives the
// *production* PlayerActionAdapter over every movement clip the shipped GLB
// catalog marks as stance-calibrated, and measures the one thing the IK-free
// claim depends on: does a supporting foot stay still in world space while the
// root advances?
//
// It also proves the execution gate is real: a movement clip with a support
// track that is not calibrated is rejected (`feasible == false`) instead of
// being played with an unexplained slide.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "foot_support.hpp"
#include "football/animation/library.hpp"
#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_motion.hpp"
#include "player/execution/player_baked_body_pose.hpp"
#include "player/execution/player_locomotion_animation.hpp"

using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

constexpr float kTick = 0.01f;

PlayerBodyProfile Body() {
  PlayerDefinition definition;
  definition.height = 1.80f;
  definition.weight = 75.0f;
  return MakePlayerBodyProfile(definition);
}

PlayerMotionProfile Profile() {
  return PlayerMotionProfile{8.0f, 6.0f, 12.0f, 6.0f};
}

// Only the stable Stance phase claims a planted foot; Landing and Takeoff are
// real foot motion (the handover), not slide.
bool Stance(const FootSupportFrame& frame, bool left) {
  return (left ? frame.left : frame.right) == FootPhase::Stance;
}

// A clip is a straight gait when its own root track never turns more than
// 10 degrees away from the mean heading.
bool StraightRoot(const AnimationClip& clip) {
  if (clip.root_positions.size() < 2) return false;
  const Vector3 travel =
      (clip.root_positions.back() - clip.root_positions.front()).Get2D();
  if (travel.GetLength() <= 1e-3f) return false;
  const Vector3 direction = travel.GetNormalized();
  for (std::size_t f = 0; f + 1 < clip.root_positions.size(); ++f) {
    const Vector3 step =
        (clip.root_positions[f + 1] - clip.root_positions[f]).Get2D();
    if (step.GetLength() <= 1e-4f) continue;
    // 10 degrees of heading deviation is a turn, not a straight gait.
    if (step.GetNormalized().GetDotProduct(direction) < 0.985f) return false;
  }
  return true;
}

bool OrdinaryStraightGait(const AnimationClip& clip) {
  const bool ordinary =
      clip.name.find("/movement/sprint/") != std::string::npos ||
      clip.name.find("/movement/walk/") != std::string::npos ||
      clip.name.find("autogen") != std::string::npos;
  const bool ball_control =
      clip.name.find("dribble") != std::string::npos ||
      clip.name.find("special") != std::string::npos ||
      clip.name.find("holdball") != std::string::npos;
  return ordinary && !ball_control && StraightRoot(clip);
}

}  // namespace

TEST_CASE("a calibrated real clip keeps its support foot planted without IK",
          "[loco][ikfree][catalog][adapter]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));

  const PlayerBodyProfile body = Body();
  int tested = 0;
  int real_glb_tested = 0;
  int rejected_clips = 0;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;
    if (!clip.motion_quality.tracked) continue;
    // The gate must be a decision, not a comment: a movement clip with a
    // support track that failed calibration is rejected.
    if (!clip.motion_quality.ik_free_usable) {
      PlayerActionControl control;
      control.desired_velocity = Vector3(0, -1.0f, 0);
      const AdaptedMotionSample rejected = PlayerActionAdapter::Evaluate(
          clip, PlayerKinematicState{}, Profile(), body, control,
          PlayerPlaybackState{}, kTick);
      CHECK_FALSE(rejected.feasible);
      CHECK(rejected.diagnostics.fallback_used);
      ++rejected_clips;
      continue;
    }

    const float reference_speed = LocomotionEffectiveGaitSpeed(
        clip, body.height_scale);
    if (reference_speed <= 0.5f) continue;
    // Turning clips pivot the support foot by design; the straight ordinary
    // gait is the migration target this acceptance covers. RM-1 repaired the
    // first real sprint half-cycle into a full IK-free cycle, so the positive
    // branch must now be exercised.
    if (!OrdinaryStraightGait(clip)) continue;
    ImportedAnimationClip verification(clip);
    DeriveFootSupport(verification);

    PlayerActionControl control;
    control.desired_velocity = Vector3(0, -reference_speed, 0);
    control.idle_facing = Vector3(0, -1, 0);

    PlayerKinematicState state;
    state.bodyFacing = Vector3(0, -1, 0);
    // Start already moving at the calibrated speed: the assertion is about the
    // steady-state gait, not the acceleration ramp.
    state.velocity = control.desired_velocity;
    PlayerPlaybackState playback;

    Vector3 previous_left(0);
    Vector3 previous_right(0);
    FootSupportFrame previous_support;
    bool have_previous = false;
    float worst_left = 0.0f;
    float worst_right = 0.0f;
    float max_joint_step = 0.0f;
    PoseFrame previous_pose;

    constexpr int kTicks = 240;
    for (int tick = 0; tick < kTicks; ++tick) {
      const AdaptedMotionSample sample = PlayerActionAdapter::Evaluate(
          clip, state, Profile(), body, control, playback, kTick);
      REQUIRE(sample.feasible);
      CHECK(sample.diagnostics.calibrated_ik_free);
      CHECK(sample.stride_scale == Catch::Approx(1.0f).margin(1e-6f));
      CHECK(sample.playback_rate <= kLocomotionMaxPlaybackRate + 1e-4f);
      // Root and published velocity are the same motion.
      CHECK(sample.root_delta.GetLength() ==
            Catch::Approx(sample.velocity.GetLength() * kTick).margin(1e-5f));

      state.position += sample.root_delta;
      state.velocity = sample.velocity;
      playback.frame_time = sample.clip_time;
      playback.stride_scale = sample.stride_scale;

      const BakedBodyPose world = EvaluateBakedBodyPose(
          sample.pose, state, sample.base_angle, body);
      const Vector3 left = world.joints[BodyPart::left_ankle];
      const Vector3 right = world.joints[BodyPart::right_ankle];
      const auto support = SampleFootSupport(verification, sample.clip_time);

      if (have_previous) {
        if (Stance(previous_support, true) &&
            Stance(support, true)) {
          worst_left =
              std::max(worst_left, (left - previous_left).Get2D().GetLength());
        }
        if (Stance(previous_support, false) &&
            Stance(support, false)) {
          worst_right =
              std::max(worst_right, (right - previous_right).Get2D().GetLength());
        }
        // Joint continuity: the real clip must not pop between ticks.
        for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
          auto before = previous_pose.orientations[joint];
          auto after = sample.pose.orientations[joint];
          const float dot = before.MakeSameNeighborhood(after);
          max_joint_step = std::max(
              max_joint_step, 2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f)));
        }
      }
      previous_left = left;
      previous_right = right;
      previous_support = support;
      previous_pose = sample.pose;
      have_previous = true;
    }

    // At the calibrated speed the stance foot's clip-space backward travel and
    // the root's forward travel cancel. Tolerance is one GLB frame of
    // discretisation, not a licence for slide.
    INFO("clip " << clip.name << " reference " << reference_speed << " m/s");
    CHECK(worst_left <= 0.015f);
    CHECK(worst_right <= 0.015f);
    CHECK(max_joint_step <= 1.5f);
    ++tested;
    // A repaired GLB keeps its canonical name, which is an absolute source
    // path; generated clips are named "autogen ...".
    if (!clip.name.starts_with("autogen")) ++real_glb_tested;
  }
  CHECK(rejected_clips > 0);
  // RM-1: the positive branch is a gate, not a note. At least one real catalog
  // gait must execute without IK, and at least one of them must be a GLB
  // (the generated clips are also valid, but they are not the asset proof).
  REQUIRE(tested > 0);
  REQUIRE(real_glb_tested > 0);
}

// RM-4: the production composition. The adapter samples the clip root; the
// caller commits it when executable and uses the capability profile's fallback
// (capped by the gait's playable speed) when it is not. The loop must
// accelerate, hold a steady cadence and brake without a root discontinuity.
TEST_CASE("RM-4: a repaired real gait accelerates and brakes through the adapter",
          "[loco][ikfree][catalog][adapter][rm4]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));
  const AnimationClip* clip = nullptr;
  for (const AnimationClip& candidate : library.Clips()) {
    if (candidate.metadata.action_type != e_DefString_Movement) continue;
    if (!candidate.motion_quality.ik_free_usable) continue;
    if (candidate.name.starts_with("autogen")) continue;
    if (!OrdinaryStraightGait(candidate)) continue;
    clip = &candidate;
    break;
  }
  REQUIRE(clip != nullptr);

  const PlayerBodyProfile body = Body();
  const PlayerMotionProfile profile = Profile();
  PlayerActionControl control;
  control.idle_facing = Vector3(0, -1, 0);
  control.authority = PlayerMotionAuthority::Locomotion;

  PlayerKinematicState state;
  state.bodyFacing = Vector3(0, -1, 0);
  PlayerPlaybackState playback;
  state.velocity = Vector3(0);
  const float playable = clip->motion_quality.root_speed *
                         body.height_scale * kLocomotionMaxPlaybackRate;
  INFO("clip " << clip->name << " playable " << playable);

  int root_ticks = 0;
  float previous_speed = 0.0f;
  float worst_acceleration = 0.0f;
  const auto one_tick = [&](const Vector3& desired) {
    control.desired_velocity = desired;
    const AdaptedMotionSample sample =
        PlayerActionAdapter::Evaluate(*clip, state, profile, body, control,
                                      playback, kTick);
    if (sample.feasible) {
      ++root_ticks;
      state.position += sample.root_delta;
      state.velocity = sample.velocity;
      playback.frame_time = sample.continuous_clip_time;
      return;
    }
    // Mirrors Player::CommitLocomotion's compatibility fallback: brake toward
    // the gait's playable range, never chase a speed this clip cannot play.
    Vector3 fallback = desired;
    if (sample.diagnostics.playable_speed > 1e-3f &&
        fallback.GetLength() > sample.diagnostics.playable_speed) {
      fallback = fallback.GetNormalized(control.idle_facing) *
                 sample.diagnostics.playable_speed;
    }
    state = PlayerMotion::StepContinuous(state, profile, fallback,
                                         control.idle_facing, kTick)
                .state;
  };

  for (int tick = 0; tick < 300; ++tick) {
    one_tick(Vector3(0, -profile.max_speed, 0));
    const float speed = state.velocity.GetLength();
    const float delta = std::fabs(speed - previous_speed);
    if (tick > 0) {
      worst_acceleration = std::max(worst_acceleration, delta / kTick);
      // A direction change is not in this straight test, so every step is a
      // pure speed change and must stay inside the profile.
      CHECK(delta <= profile.acceleration * kTick + 1e-4f);
    }
    CHECK(speed <= profile.max_speed + 1e-4f);
    previous_speed = speed;
  }
  CHECK(root_ticks > 0);
  CHECK(state.velocity.GetLength() > 0.5f);

  // Now stop. The same composition must brake, not teleport to zero.
  previous_speed = state.velocity.GetLength();
  for (int tick = 0; tick < 300; ++tick) {
    one_tick(Vector3(0));
    const float speed = state.velocity.GetLength();
    const float delta = std::fabs(speed - previous_speed);
    CHECK(delta <= profile.braking * kTick + 1e-4f);
    previous_speed = speed;
  }
  CHECK(state.velocity.GetLength() <= kLocomotionStopSpeed + 1e-3f);
  CHECK(worst_acceleration <= profile.acceleration + 1e-2f);
}

// RM-F4: the repaired cycle changes frame count and native root speed. The
// selection metadata must stay reachable and the source/mirror pair must remain
// a symmetric description of the same gait.
TEST_CASE("RM-F4: a repaired gait keeps its metadata and mirror symmetry",
          "[loco][ikfree][catalog][rmf4]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));
  int tested = 0;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;
    if (!clip.motion_quality.ik_free_usable) continue;
    if (clip.name.starts_with("autogen")) continue;
    ++tested;
    INFO("clip " << clip.name);
    CHECK(clip.frame_count == clip.poses.size());
    CHECK(clip.frame_count > 25);
    CHECK(clip.metadata.touch_frame == -1);
    CHECK_FALSE(clip.has_contact);
    CHECK(clip.touches.empty());
    const float reachable =
        clip.motion_quality.root_speed * 2.2f;
    CHECK(clip.metadata.incoming_velocity <= reachable + 1e-3f);
    CHECK(clip.metadata.outgoing_velocity <= reachable + 1e-3f);
    CHECK(clip.metadata.incoming_movement.GetLength() <= reachable + 1e-3f);
    CHECK(clip.metadata.outgoing_movement.GetLength() <= reachable + 1e-3f);
    const Vector3 translation =
        clip.root_positions.back() - clip.root_positions.front();
    CHECK((clip.metadata.translation - translation).GetLength() <= 1e-4f);

    // The catalog's mirror entry is the other half of the same description.
    const bool is_mirror = clip.name.ends_with("_mirror");
    const std::string partner_name =
        is_mirror ? clip.name.substr(0, clip.name.size() - 7)
                  : clip.name + "_mirror";
    const AnimationClip* partner = nullptr;
    for (const AnimationClip& candidate : library.Clips()) {
      if (candidate.name == partner_name) {
        partner = &candidate;
        break;
      }
    }
    REQUIRE(partner != nullptr);
    CHECK(partner->frame_count == clip.frame_count);
    CHECK(partner->motion_quality.ik_free_usable);
    CHECK(partner->motion_quality.root_speed ==
          Catch::Approx(clip.motion_quality.root_speed).margin(1e-4f));
    for (std::size_t frame = 0; frame < clip.root_positions.size(); ++frame) {
      CHECK(partner->root_positions[frame].coords[0] ==
            Catch::Approx(-clip.root_positions[frame].coords[0]).margin(1e-4f));
      CHECK(partner->root_positions[frame].coords[1] ==
            Catch::Approx(clip.root_positions[frame].coords[1]).margin(1e-4f));
    }
  }
  REQUIRE(tested > 0);
}
