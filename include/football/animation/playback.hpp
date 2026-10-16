#ifndef FOOTBALL_ANIMATION_PLAYBACK_HPP
#define FOOTBALL_ANIMATION_PLAYBACK_HPP

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/animation/transition.hpp"

namespace football::animation {

// ANIM-2 A11.1: moved out of sim/player. Animation owns the playback state
// type; the Player owns the instance and the authoritative action clock.

// ANIM-2 A10.1 — Player-owned playback instance.
//
// One instance per Player. `Play()` is the production entry: the caller supplies
// the authoritative clip, integer frame (from PlayerActionState) and base yaw,
// and receives the single FinalPose for this tick. It samples the pose, advances
// the cross-clip transition at most once and stores the result; it never
// advances the action clock itself. `Pose()`/`BaseAngle()` are pure reads that
// change nothing.
//
// `Tick()`/`AdvanceCursor()` are the self-advancing test helpers. They share the
// sampling core but are not part of the production interface, so they cannot
// become a second time authority.
struct PlaybackState {
  AnimationId clip_id = -1;
  int frame = 0;
  bool have_pose = false;
  PoseFrame final_pose;
  float final_base_angle = 0.0f;
  AnimationTransitionState transition;

  void Reset() {
    clip_id = -1;
    frame = 0;
    have_pose = false;
    final_pose = PoseFrame{};
    final_base_angle = 0.0f;
    transition.Reset();
  }

  // Production: one real playback update for this tick. A clip change blends
  // from the last final pose over `blend_ticks`.
  const PoseFrame& Play(const AnimationClip& clip, int at_frame, float base_angle,
                        int blend_ticks) {
    return Apply(clip, at_frame, base_angle, blend_ticks);
  }

  // The single FinalPose the rest of the tick must read. Pure read.
  const PoseFrame& Pose() const { return final_pose; }
  float BaseAngle() const { return final_base_angle; }

  // Test-only: self-advancing playback. Advances the internal cursor one frame
  // per call; never used by production.
  const PoseFrame& Tick(const AnimationClip& clip, int blend_ticks) {
    const int at_frame = clip.id == clip_id ? frame : 0;
    const PoseFrame& pose = Apply(clip, at_frame, 0.0f, blend_ticks);
    if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
    return pose;
  }

  // Test-only: advance the internal cursor for the next self-advancing Tick.
  void AdvanceCursor(const AnimationClip& clip) {
    if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
  }

 private:
  // The sampling core shared by the production and test entries.
  const PoseFrame& Apply(const AnimationClip& clip, int at_frame, float base_angle,
                         int blend_ticks) {
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
};

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_PLAYBACK_HPP
