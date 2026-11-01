#include "football/player/execution/player_action_adapter.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "football/animation/foot_support.hpp"
#include "football/animation/pose_sampling.hpp"
#include "player/execution/player_locomotion_animation.hpp"

namespace {

football::math::Vector3 RotateLocalDelta(const football::math::Vector3& local,
                                         float base_angle) {
  return local.Get2D().GetRotated2D(base_angle);
}

}  // namespace

AdaptedMotionSample PlayerActionAdapter::Evaluate(
    const AnimationClip& clip, const PlayerKinematicState& current,
    const PlayerMotionProfile& capability,
    const football::player::PlayerBodyProfile& body,
    const PlayerActionControl& control,
    const PlayerPlaybackState& playback, float dt) {
  assert(dt > 0.0f);
  AdaptedMotionSample sample;
  sample.clip_id = static_cast<AnimationId>(clip.id);
  sample.base_angle = control.base_angle;

  if (clip.poses.empty()) {
    // No skeleton to perform the motion: the caller must keep the
    // compatibility path rather than commit a root without a pose.
    sample.feasible = false;
    sample.diagnostics.fallback_used = true;
    return sample;
  }

  const bool locomotion =
      control.authority == PlayerMotionAuthority::Locomotion;
  football::math::Vector3 reference_delta = control.desired_velocity * dt;
  if (!locomotion) {
    // RMA-5: an action plan already produced its trajectory, including the
    // smuggle corrections. That plan is the candidate; the same constraint
    // core validates it instead of adding a second hidden displacement.
    // A supplied zero is a real zero (planted touch, stop), not "missing".
    reference_delta =
        control.has_reference_delta
            ? control.reference_delta
            : (clip.root_positions.size() >= 2
                   ? RotateLocalDelta(clip.root_positions[1] -
                                          clip.root_positions[0],
                                      control.base_angle)
                   : football::math::Vector3(0));
  }

  const float reference_speed =
      locomotion ? LocomotionEffectiveGaitSpeed(clip, body.height_scale)
                 : 0.0f;
  // The gait's playable speed is a desired bound, not a post-integration
  // scale. Feeding it into the motion constraint keeps acceleration and
  // braking inside the profile; a body already faster decelerates over time
  // instead of snapping to the cap in one tick.
  football::math::Vector3 motion_desired = control.desired_velocity;
  bool gait_limited = false;
  if (locomotion && clip.motion_quality.ik_free_usable &&
      reference_speed > 1e-3f) {
    const float max_gait_speed =
        reference_speed * kLocomotionMaxPlaybackRate;
    if (motion_desired.GetLength() > max_gait_speed) {
      motion_desired = motion_desired.GetNormalized(control.idle_facing) *
                       max_gait_speed;
      gait_limited = true;
    }
  }

  PlayerMotionConstraintInput motion;
  motion.reference_delta = locomotion ? motion_desired * dt : reference_delta;
  motion.current = current;
  motion.body = body;
  motion.capability = capability;
  motion.desired_velocity = motion_desired;
  motion.authority = control.authority;
  motion.idle_facing = control.idle_facing;
  motion.dt = dt;
  const PlayerMotionConstraintResult constrained =
      PlayerMotionAdapter::Step(motion);

  sample.root_delta = constrained.achieved_delta;
  sample.velocity = constrained.state.velocity;

  const float desired_speed = control.desired_velocity.GetLength();
  const float achieved_speed = constrained.state.velocity.GetLength();
  sample.diagnostics.capability_limited =
      desired_speed > capability.max_speed + 1e-4f || gait_limited ||
      (control.authority != PlayerMotionAuthority::Locomotion &&
       constrained.constrained);

  football::math::Vector3 reference_local = football::math::Vector3(0);
  if (locomotion) {
    // Cadence-only: the clip plays at the speed the body achieved, with its
    // authored stride. Changing the stride would require solving the leg -
    // exactly the runtime IK this stage removes.
    const float stride_scale = 1.0f;
    LocomotionPlaybackRequest previous;
    previous.clip_id = sample.clip_id;
    previous.frame_time = playback.frame_time;
    previous.stride_scale = playback.stride_scale;
    const LocomotionPlaybackRequest request = AdvanceLocomotionPlayback(
        previous, sample.clip_id, clip, achieved_speed, reference_speed,
        playback.entry_frame_time, dt, stride_scale);

    sample.clip_time = request.frame_time;
    sample.gait_phase = request.gait_phase;
    sample.playback_rate = request.playback_rate;
    sample.stride_scale = request.stride_scale;
    sample.looped = request.looped;
    reference_local = LocomotionReferenceDelta(
        clip.root_positions, playback.frame_time, request.frame_time);
  } else {
    // One-shot actions keep their action clock as the clip-time authority.
    // The adapter validates the plan's displacement and samples the pose at
    // the phase the action cursor already owns - it never advances it, so a
    // touch frame cannot be shifted by an adaptation rate.
    sample.clip_time = playback.frame_time;
    const float period = std::max(
        static_cast<float>(clip.frame_count > 1 ? clip.frame_count - 1 : 1),
        1.0f);
    sample.gait_phase = std::clamp(playback.frame_time / period, 0.0f, 1.0f);
    sample.playback_rate = 1.0f;
    sample.stride_scale = 1.0f;
    sample.looped = false;
  }

  sample.pose = SamplePose(clip, sample.clip_time);
  sample.support = SampleFootSupport(clip, sample.clip_time);
  sample.reference_delta_local = reference_local;
  sample.diagnostics.reference_delta =
      locomotion ? RotateLocalDelta(reference_local, control.base_angle)
                 : reference_delta;
  sample.diagnostics.adapted_delta = constrained.achieved_delta;
  sample.diagnostics.final_delta = constrained.achieved_delta;
  sample.diagnostics.playback_rate = sample.playback_rate;
  sample.diagnostics.stride_scale = sample.stride_scale;
  sample.diagnostics.reach_limited = false;
  sample.diagnostics.ik_free_pose =
      locomotion && clip.motion_quality.ik_free_usable;
  sample.diagnostics.calibrated_ik_free =
      locomotion && clip.motion_quality.stance_calibrated;
  // The strict source gate controls execution. A clip whose own root track and
  // support foot disagree is rejected: stance calibration is a diagnostic that
  // the asset could be fixed, not permission to ship the slide.
  if (locomotion && clip.metadata.action_type == e_DefString_Movement &&
      !clip.foot_support.empty() &&
      !clip.motion_quality.ik_free_usable) {
    sample.feasible = false;
    sample.diagnostics.fallback_used = true;
  }
  return sample;
}

PredictedMotion PlayerActionAdapter::Predict(
    const AnimationClip& clip, const PlayerKinematicState& initial,
    const PlayerMotionProfile& capability,
    const football::player::PlayerBodyProfile& body,
    const PlayerActionControl& control, const PlayerPlaybackState& playback,
    float dt, football::sim::TickSpan horizon) {
  PredictedMotion predicted;
  predicted.state = initial;
  PlayerPlaybackState cursor = playback;
  for (football::sim::TickSpan elapsed{};
       elapsed < horizon; elapsed += football::sim::TickSpan{1}) {
    const AdaptedMotionSample sample =
        Evaluate(clip, predicted.state, capability, body, control, cursor, dt);
    if (!sample.feasible) {
      predicted.sample = sample;
      return predicted;
    }
    predicted.state.position += sample.root_delta;
    predicted.state.velocity = sample.velocity;
    predicted.sample = sample;
    cursor.frame_time = sample.clip_time;
    cursor.stride_scale = sample.stride_scale;
  }
  return predicted;
}
