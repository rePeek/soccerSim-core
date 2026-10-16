// ANIM-2 A10.1/A11.1/A12.4 — the Player-owned playback instance, moved verbatim
// out of playback.hpp. Sampling, transition and cursor semantics are unchanged.

#include "football/animation/playback.hpp"

namespace football::animation {

void PlaybackState::Reset() {
  clip_id = -1;
  frame = 0;
  have_pose = false;
  final_pose = PoseFrame{};
  final_base_angle = 0.0f;
  transition.Reset();
}

const PoseFrame& PlaybackState::Play(const AnimationClip& clip, int at_frame,
                                     float base_angle, int blend_ticks) {
  return Apply(clip, at_frame, base_angle, blend_ticks);
}

const PoseFrame& PlaybackState::Tick(const AnimationClip& clip, int blend_ticks) {
  const int at_frame = clip.id == clip_id ? frame : 0;
  const PoseFrame& pose = Apply(clip, at_frame, 0.0f, blend_ticks);
  if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
  return pose;
}

void PlaybackState::AdvanceCursor(const AnimationClip& clip) {
  if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
}

const PoseFrame& PlaybackState::Apply(const AnimationClip& clip, int at_frame,
                                      float base_angle, int blend_ticks) {
  if (clip.id != clip_id) {
    if (have_pose) {
      transition.Begin(final_pose, final_base_angle, blend_ticks);
    } else {
      transition.Reset();
    }
    clip_id = clip.id;
  }
  const PoseFrame sample = SamplePose(clip, static_cast<float>(at_frame));
  if (have_pose) {
    final_pose = transition.Advance(sample, base_angle);
    final_base_angle = transition.blended_base_angle;
  } else {
    final_pose = sample;
    final_base_angle = base_angle;
  }
  have_pose = true;
  frame = at_frame;
  return final_pose;
}

}  // namespace football::animation
