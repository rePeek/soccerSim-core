#ifndef FOOTBALL_SIM_PLAYER_PLAYER_PLAYBACK_STATE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_PLAYBACK_STATE_HPP

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/animation/pose_sampling.hpp"
#include "sim/player/animation_transition_state.hpp"

// ANIM-1 A3 — Player-owned animation playback state.
//
// The Player owns the clip cursor, the cross-clip transition and the final pose;
// the Animation module stays stateless (clip data + BakedAnimationSelector +
// SamplePose + BlendPose). This type depends on no Humanoid, Ball, Referee,
// Simulation or RNG.
struct PlayerPlaybackState {
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

  // Production sampling: the caller supplies the authoritative integer frame
  // and base yaw (the action clip is selected elsewhere for now). A clip change
  // blends from the last final pose over `blend_ticks`.
  const PoseFrame& Sample(const AnimationClip& clip, int at_frame, float base_angle,
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

  // Self-advancing variant for tests: advances the internal integer cursor one
  // frame per call.
  const PoseFrame& Tick(const AnimationClip& clip, int blend_ticks) {
    const int at_frame = clip.id == clip_id ? frame : 0;
    const PoseFrame& pose = Sample(clip, at_frame, 0.0f, blend_ticks);
    if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
    return pose;
  }

  // Advance the internal cursor for the next self-advancing Tick.
  void AdvanceCursor(const AnimationClip& clip) {
    if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
  }
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_PLAYBACK_STATE_HPP
