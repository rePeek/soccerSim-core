#ifndef FOOTBALL_SIM_PLAYER_PLAYER_PLAYBACK_STATE_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_PLAYBACK_STATE_HPP

#include "football/animation/clip.hpp"
#include "football/animation/pose_sampling.hpp"
#include "sim/player/animation_transition_state.hpp"

// ANIM-1 A3 — Player-owned animation playback state.
//
// The Player owns the clip cursor, the cross-clip transition and the final pose;
// the Animation module stays stateless (clip data + BakedAnimationSelector +
// SamplePose + BlendPose). This type depends on no Humanoid, Ball, Referee,
// Simulation or RNG. It is the destination for the playback state that today
// still lives in HumanoidBase (`currentAnim.frameNum`, `movement_transition_`,
// `final_blended_pose_`); this cut introduces and tests it before the
// production migration.
struct PlayerPlaybackState {
  AnimationId clip_id = -1;
  int frame = 0;
  bool have_pose = false;
  PoseFrame final_pose;
  AnimationTransitionState transition;

  void Reset() {
    clip_id = -1;
    frame = 0;
    have_pose = false;
    final_pose = PoseFrame{};
    transition.Reset();
  }

  // Advance one tick for `clip` and return the final pose. A clip change blends
  // from the last final pose over `blend_ticks`; the integer `frame` cursor is
  // the only playback time (no separate float clock).
  const PoseFrame& Tick(const AnimationClip& clip, int blend_ticks) {
    if (clip.id != clip_id) {
      if (have_pose) {
        transition.Begin(final_pose, 0.0f, blend_ticks);
      } else {
        transition.Reset();
      }
      clip_id = clip.id;
      frame = 0;
    }
    const PoseFrame sample = SamplePose(clip, static_cast<float>(frame));
    final_pose = have_pose ? transition.Advance(sample, 0.0f) : sample;
    have_pose = true;
    if (frame < static_cast<int>(clip.poses.size()) - 1) ++frame;
    return final_pose;
  }
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_PLAYBACK_STATE_HPP
