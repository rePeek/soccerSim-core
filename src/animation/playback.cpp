// ANIM-2 A10.1/A11.1/A12.4 — the Player-owned playback instance, moved verbatim
// out of playback.hpp. Sampling, transition and cursor semantics are unchanged.
//
// M2 adds the continuous-time entry: the pose is sampled from a fractional clip
// time so motion-driven locomotion can play a stride faster than one frame per
// tick. The integer cursor is derived from that time for audit only.

#include "football/animation/playback.hpp"

#include <cmath>

#include "football/animation/pose_sampling.hpp"

namespace football::animation {

void PlaybackState::Reset() {
  clip_id = -1;
  frame = 0;
  frame_time = 0.0f;
  have_pose = false;
  final_pose = PoseFrame{};
  final_base_angle = 0.0f;
  blend_source_pose = PoseFrame{};
  blend_source_base_angle = 0.0f;
  blend_source_valid = false;
  transition.Reset();
}

void PlaybackState::SetBlendSource(const PoseFrame& pose, float base_angle) {
  blend_source_pose = pose;
  blend_source_base_angle = base_angle;
  blend_source_valid = true;
}

const PoseFrame& PlaybackState::Play(const AnimationClip& clip, int at_frame,
                                     float base_angle, int blend_ticks) {
  return Apply(clip, static_cast<float>(at_frame), base_angle, blend_ticks);
}

const PoseFrame& PlaybackState::PlayAtTime(const AnimationClip& clip,
                                           float at_frame_time, float base_angle,
                                           int blend_ticks) {
  return Apply(clip, at_frame_time, base_angle, blend_ticks);
}

const PoseFrame& PlaybackState::Tick(const AnimationClip& clip, int blend_ticks) {
  const int at_frame = clip.id == clip_id ? frame : 0;
  const PoseFrame& pose = Apply(clip, static_cast<float>(at_frame), 0.0f, blend_ticks);
  if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
  return pose;
}

void PlaybackState::AdvanceCursor(const AnimationClip& clip) {
  if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
}

const PoseFrame& PlaybackState::Apply(const AnimationClip& clip, float at_frame_time,
                                      float base_angle, int blend_ticks) {
  if (clip.id != clip_id) {
    // M3: blend from the Player's last Final Pose when it published one, so a
    // switch starts from the skeleton that was actually drawn, not from the raw
    // animation sample the Player corrects afterwards.
    if (blend_source_valid) {
      transition.Begin(blend_source_pose, blend_source_base_angle, blend_ticks);
    } else if (have_pose) {
      transition.Begin(final_pose, final_base_angle, blend_ticks);
    } else {
      transition.Reset();
    }
    clip_id = clip.id;
  }
  const float clamped_time = PoseFrameTimeClamped(clip, at_frame_time);
  const PoseFrame sample = SamplePose(clip, clamped_time);
  if (have_pose) {
    final_pose = transition.Advance(sample, base_angle);
    final_base_angle = transition.blended_base_angle;
  } else {
    final_pose = sample;
    final_base_angle = base_angle;
  }
  have_pose = true;
  frame_time = clamped_time;
  frame = static_cast<int>(std::lround(clamped_time));
  return final_pose;
}

}  // namespace football::animation
