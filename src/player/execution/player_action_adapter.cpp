#include "football/player/execution/player_action_adapter.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "football/animation/foot_support.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/animation/reference_sample.hpp"
#include "player/execution/player_locomotion_animation.hpp"

namespace {

// RM-3: the clip's root direction may differ from the control direction, but
// only by a turning allowance; a larger mismatch means "select another clip",
// not "rotate the root while keeping the pose".
constexpr float kLocomotionRootDirectionTolerance = 0.35f;

football::math::Vector3 RotateLocalDelta(const football::math::Vector3& local,
                                         float base_angle) {
  return local.Get2D().GetRotated2D(base_angle);
}

// One acceleration/braking step of the capability profile toward `target`.
// The root sample is scaled by the resulting playback rate, so braking and
// acceleration happen inside the same clip time the pose is sampled at.
float ProfileSpeedStep(float current, float target,
                       const PlayerMotionProfile& capability, float dt) {
  if (target >= current) {
    return std::min(target, current + capability.acceleration * dt);
  }
  return std::max(target, current - capability.braking * dt);
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
    sample.status = AdaptedMotionSample::Status::InvalidReference;
    sample.feasible = false;
    sample.diagnostics.fallback_used = true;
    return sample;
  }

  const bool locomotion =
      control.authority == PlayerMotionAuthority::Locomotion;
  const float reference_speed =
      locomotion ? LocomotionEffectiveGaitSpeed(clip, body.height_scale)
                 : 0.0f;
  // The root track is the displacement source; the stance-derived gait speed
  // is the cadence reference the compatibility path follows. They agree on a
  // repaired clip, but the committed delta is measured from the root track.
  const float root_speed =
      locomotion ? LocomotionClipReferenceSpeed(clip) * body.height_scale
                 : 0.0f;

  // RM-3: ordinary locomotion is driven by the clip's own root track when the
  // source passes the strict IK-free gate. The control supplies the target
  // speed; the capability profile decides how fast the body may approach it;
  // the clip supplies the trajectory, the pose and the foot timing. If no
  // bounded playback rate can produce that motion, the sample is explicitly
  // not executable and the caller keeps its compatibility fallback.
  if (locomotion && clip.motion_quality.ik_free_usable && clip.looping &&
      reference_speed > 1e-3f && root_speed > 1e-3f &&
      clip.root_positions.size() >= 2) {
    const football::math::Vector3 desired = control.desired_velocity.Get2D();
    const float desired_speed = desired.GetLength();
    const float current_speed = current.velocity.GetLength();
    const float period =
        std::max(static_cast<float>(clip.root_positions.size() - 1), 1.0f);
    const float start = std::max(playback.frame_time, 0.0f);
    sample.diagnostics.ik_free_pose = true;
    sample.diagnostics.calibrated_ik_free =
        clip.motion_quality.stance_calibrated;

    if (desired_speed <= kLocomotionStopSpeed &&
        current_speed <= kLocomotionStopSpeed) {
      const ReferenceAnimationSample still =
          SampleAnimationStep(clip, start, start);
      sample.velocity = football::math::Vector3(0);
      sample.pose = still.pose;
      sample.support = SampleFootSupport(clip, still.pose_time);
      sample.clip_time = still.pose_time;
      sample.continuous_clip_time = start;
      sample.gait_phase = period > 0.0f ? still.pose_time / period : 0.0f;
      sample.playback_rate = 0.0f;
      sample.status = AdaptedMotionSample::Status::Accepted;
      sample.feasible = true;
      sample.diagnostics.playback_rate = 0.0f;
      return sample;
    }

    // The profile's next speed, bounded by the player and by the clip's
    // playable range. `target_speed` is what the body may aim at this tick;
    // the sampled root still decides how far that speed moves it.
    const float root_playable =
        root_speed * kLocomotionMaxPlaybackRate;
    sample.diagnostics.playable_speed = root_playable;
    const float target_speed = std::min(
        std::min(desired_speed, capability.max_speed), root_playable);
    const float feasible_speed =
        ProfileSpeedStep(current_speed, target_speed, capability, dt);
    const float rate = feasible_speed / root_speed;
    if (rate > kLocomotionMaxPlaybackRate + 1e-3f) {
      // The body is still above what this gait can play. Braking belongs to
      // the compatibility path until the body is inside the clip's range.
      sample.status = AdaptedMotionSample::Status::NeedAnotherClip;
      sample.feasible = false;
      sample.diagnostics.fallback_used = true;
      sample.diagnostics.playback_rate = rate;
      return sample;
    }

    const float next = start + rate * dt * 100.0f;
    const ReferenceAnimationSample reference =
        SampleAnimationStep(clip, start, next);
    const football::math::Vector3 local_scaled =
        reference.root_delta * body.height_scale;
    const football::math::Vector3 proposed =
        RotateLocalDelta(local_scaled, control.base_angle);
    const football::math::Vector3 velocity = proposed / dt;
    const float speed = velocity.GetLength();
    if (desired_speed > kLocomotionStopSpeed && speed > kLocomotionStopSpeed) {
      const football::math::Vector3 desired_dir =
          desired.GetNormalized(control.idle_facing);
      const float direction_error =
          std::fabs(velocity.GetNormalized().GetAngle2D(desired_dir));
      if (direction_error > kLocomotionRootDirectionTolerance) {
        sample.status = AdaptedMotionSample::Status::NeedAnotherClip;
        sample.feasible = false;
        sample.diagnostics.fallback_used = true;
        return sample;
      }
    }
    const float speed_delta =
        (velocity - current.velocity.Get2D()).GetLength();
    const float acceleration_limit =
        speed >= current_speed ? capability.acceleration : capability.braking;
    if (speed > capability.max_speed + 1e-4f ||
        speed_delta > acceleration_limit * dt + 1e-4f) {
      sample.status = AdaptedMotionSample::Status::CapabilityLimited;
      sample.feasible = false;
      sample.diagnostics.fallback_used = true;
      sample.diagnostics.capability_limited = true;
      return sample;
    }

    sample.root_delta = proposed;
    sample.velocity = velocity;
    sample.reference_delta_local = reference.root_delta;
    sample.pose = reference.pose;
    sample.support = SampleFootSupport(clip, reference.pose_time);
    sample.clip_time = reference.pose_time;
    sample.continuous_clip_time = reference.continuous_time;
    sample.gait_phase =
        period > 0.0f ? reference.pose_time / period : 0.0f;
    sample.playback_rate = rate;
    sample.stride_scale = 1.0f;
    sample.looped = reference.looped;
    sample.status = AdaptedMotionSample::Status::Accepted;
    sample.feasible = true;
    sample.diagnostics.reference_delta =
        RotateLocalDelta(local_scaled, control.base_angle);
    sample.diagnostics.adapted_delta = proposed;
    sample.diagnostics.final_delta = proposed;
    sample.diagnostics.playback_rate = rate;
    sample.diagnostics.stride_scale = 1.0f;
    sample.diagnostics.capability_limited =
        desired_speed > root_playable + 1e-4f;
    return sample;
  }

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

  PlayerMotionConstraintInput motion;
  motion.reference_delta = reference_delta;
  motion.current = current;
  motion.body = body;
  motion.capability = capability;
  motion.desired_velocity = control.desired_velocity;
  motion.authority = control.authority;
  motion.idle_facing = control.idle_facing;
  motion.dt = dt;
  const PlayerMotionConstraintResult constrained =
      PlayerMotionAdapter::Step(motion);

  sample.root_delta = constrained.achieved_delta;
  sample.velocity = constrained.state.velocity;

  const float desired_speed = control.desired_velocity.GetLength();
  sample.diagnostics.capability_limited =
      desired_speed > capability.max_speed + 1e-4f ||
      (control.authority != PlayerMotionAuthority::Locomotion &&
       constrained.constrained);

  football::math::Vector3 reference_local = football::math::Vector3(0);
  if (locomotion) {
    // Compatibility playback for a movement clip outside the strict gate.
    const float achieved_speed = constrained.state.velocity.GetLength();
    LocomotionPlaybackRequest previous;
    previous.clip_id = sample.clip_id;
    previous.frame_time = playback.frame_time;
    previous.stride_scale = playback.stride_scale;
    const LocomotionPlaybackRequest request = AdvanceLocomotionPlayback(
        previous, sample.clip_id, clip, achieved_speed, reference_speed,
        playback.entry_frame_time, dt, 1.0f);

    sample.clip_time = request.frame_time;
    sample.continuous_clip_time = request.frame_time;
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
    sample.continuous_clip_time = playback.frame_time;
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
    sample.status = AdaptedMotionSample::Status::NeedAnotherClip;
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
    cursor.frame_time = sample.continuous_clip_time;
    cursor.stride_scale = sample.stride_scale;
  }
  return predicted;
}
