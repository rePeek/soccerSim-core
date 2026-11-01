// RM-F2: the IK-free acceptance must read the *final* pose, not the adapter
// sample. The production chain is:
//
//   SampleAnimationStep -> Warp -> bounded Realize -> Final Pose
//
// A clip whose sampled pose is clean can still drift if Realize clamps a joint
// faster than its allow-list. This test drives a repaired real GLB through that
// exact chain for two body heights and measures stance-foot motion in the final
// world skeleton. A support handover is allowed a larger transient budget; the
// steady stance inside a run must stay tight.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "football/animation/foot_support.hpp"
#include "football/animation/library.hpp"
#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_pose_adapter.hpp"
#include "player/execution/player_baked_body_pose.hpp"
#include "player/execution/player_locomotion_animation.hpp"
#include "player/execution/player_motion_constants.hpp"

using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

constexpr float kTick = 0.01f;
// Realize is a second bounded stage; steady stance may add one frame of
// discretisation to the adapter-level 0.015 m standard. A handover gets its own
// transient budget because a knee/ankle may legitimately turn fast while the
// other foot takes over.
constexpr float kSteadySlip = 0.03f;
constexpr float kHandoverSlip = 0.15f;
constexpr int kHandoverWindowTicks = 8;

PlayerBodyProfile Body(float height) {
  PlayerDefinition definition;
  definition.height = height;
  definition.weight = 75.0f;
  return MakePlayerBodyProfile(definition);
}

PlayerMotionProfile Profile() {
  return PlayerMotionProfile{8.0f, 6.0f, 12.0f, 6.0f};
}

bool Stance(const FootSupportFrame& frame, bool left) {
  return (left ? frame.left : frame.right) == FootPhase::Stance;
}

bool SupportChanged(const FootSupportFrame& a, const FootSupportFrame& b) {
  return a.left != b.left || a.right != b.right;
}

bool OrdinaryStraightGait(const AnimationClip& clip) {
  const bool ordinary =
      clip.name.find("/movement/sprint/") != std::string::npos ||
      clip.name.find("/movement/walk/") != std::string::npos;
  if (!ordinary) return false;
  if (clip.root_positions.size() < 2) return false;
  const Vector3 travel =
      (clip.root_positions.back() - clip.root_positions.front()).Get2D();
  if (travel.GetLength() <= 1e-3f) return false;
  const Vector3 direction = travel.GetNormalized();
  for (std::size_t frame = 0; frame + 1 < clip.root_positions.size(); ++frame) {
    const Vector3 step =
        (clip.root_positions[frame + 1] - clip.root_positions[frame]).Get2D();
    if (step.GetLength() <= 1e-4f) continue;
    if (step.GetNormalized().GetDotProduct(direction) < 0.985f) return false;
  }
  return true;
}

struct FinalPoseMetrics {
  float steady_left_slip = 0.0f;
  float steady_right_slip = 0.0f;
  float handover_left_slip = 0.0f;
  float handover_right_slip = 0.0f;
  float worst_joint_step = 0.0f;
  int limited_ticks = 0;
};

FinalPoseMetrics Drive(const AnimationClip& clip, const PlayerBodyProfile& body) {
  const float native_speed =
      LocomotionClipReferenceSpeed(clip) * body.height_scale;
  PlayerActionControl control;
  control.desired_velocity = Vector3(0, -native_speed, 0);
  control.idle_facing = Vector3(0, -1, 0);
  control.authority = PlayerMotionAuthority::Locomotion;
  control.base_angle = 0.0f;

  PlayerKinematicState state;
  state.bodyFacing = Vector3(0, -1, 0);
  state.velocity = control.desired_velocity;
  PlayerPlaybackState playback;

  PoseFrame final_pose;
  float final_base_angle = 0.0f;
  bool have_final = false;
  PoseFrame previous_final;
  FootSupportFrame previous_support;
  Vector3 previous_left(0);
  Vector3 previous_right(0);
  bool have_previous = false;
  int ticks_since_support_change = kHandoverWindowTicks + 1;

  FinalPoseMetrics metrics;
  for (int tick = 0; tick < 240; ++tick) {
    const AdaptedMotionSample sample = PlayerActionAdapter::Evaluate(
        clip, state, Profile(), body, control, playback, kTick);
    REQUIRE(sample.feasible);
    state.position += sample.root_delta;
    state.velocity = sample.velocity;
    playback.frame_time = sample.continuous_clip_time;

    if (have_previous) {
      if (SupportChanged(previous_support, sample.support)) {
        ticks_since_support_change = 0;
      } else {
        ++ticks_since_support_change;
      }
    }

    PlayerPoseWarpInput warp;
    warp.reference_pose = sample.pose;
    warp.reference_base_angle = sample.base_angle;
    warp.reference_delta =
        sample.reference_delta_local.Get2D().GetRotated2D(sample.base_angle);
    warp.achieved_delta = sample.root_delta;
    warp.body = body;
    warp.pelvis_bob_scale = 1.0f;
    warp.max_yaw_correction = kPlayerPoseWarpMaxYawPerTick;
    const PlayerPoseWarpResult warped = PlayerPoseAdapter::Warp(warp);

    PlayerPoseRealizeInput realize;
    realize.previous_pose = have_final ? final_pose : warped.pose;
    realize.target_pose = warped.pose;
    realize.body = body;
    realize.entry_weight = 0.0f;
    realize.outgoing_special_state = false;
    const PlayerPoseRealizeResult realized = PlayerPoseAdapter::Realize(realize);
    final_pose = realized.pose;
    final_base_angle = warped.base_angle;
    if (realized.limited) ++metrics.limited_ticks;

    if (have_final) {
      for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
        auto before = previous_final.orientations[joint];
        auto after = final_pose.orientations[joint];
        const float dot = before.MakeSameNeighborhood(after);
        metrics.worst_joint_step =
            std::max(metrics.worst_joint_step,
                     2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f)));
      }
    }

    const BakedBodyPose world =
        EvaluateBakedBodyPose(final_pose, state, final_base_angle, body);
    const Vector3 left = world.joints[BodyPart::left_ankle];
    const Vector3 right = world.joints[BodyPart::right_ankle];
    const bool handover = ticks_since_support_change <= kHandoverWindowTicks;
    if (have_previous) {
      if (Stance(previous_support, true) && Stance(sample.support, true)) {
        const float slip = (left - previous_left).Get2D().GetLength();
        float& target =
            handover ? metrics.handover_left_slip : metrics.steady_left_slip;
        target = std::max(target, slip);
      }
      if (Stance(previous_support, false) && Stance(sample.support, false)) {
        const float slip = (right - previous_right).Get2D().GetLength();
        float& target =
            handover ? metrics.handover_right_slip : metrics.steady_right_slip;
        target = std::max(target, slip);
      }
    }
    previous_left = left;
    previous_right = right;
    previous_support = sample.support;
    previous_final = final_pose;
    have_final = true;
    have_previous = true;
  }
  return metrics;
}

}  // namespace

TEST_CASE("RM-F2: the final pose keeps the support foot planted",
          "[loco][ikfree][catalog][finalpose][rmf2]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));
  int tested = 0;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;
    if (!clip.motion_quality.ik_free_usable) continue;
    if (clip.name.starts_with("autogen")) continue;
    if (!OrdinaryStraightGait(clip)) continue;
    for (const float height : {1.80f, 1.95f}) {
      const FinalPoseMetrics metrics = Drive(clip, Body(height));
      INFO("clip " << clip.name << " height " << height << " limited "
                   << metrics.limited_ticks << " steady L/R "
                   << metrics.steady_left_slip << "/"
                   << metrics.steady_right_slip << " handover L/R "
                   << metrics.handover_left_slip << "/"
                   << metrics.handover_right_slip);
      CHECK(metrics.steady_left_slip <= kSteadySlip);
      CHECK(metrics.steady_right_slip <= kSteadySlip);
      CHECK(metrics.handover_left_slip <= kHandoverSlip);
      CHECK(metrics.handover_right_slip <= kHandoverSlip);
      CHECK(metrics.worst_joint_step <= 1.5f);
      ++tested;
    }
  }
  REQUIRE(tested >= 2);
}
