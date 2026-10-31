#ifndef FOOTBALL_ANIMATION_PLAYBACK_HPP
#define FOOTBALL_ANIMATION_PLAYBACK_HPP

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/animation/transition.hpp"

// ANIM-2 A11.1: moved out of sim/player. Animation owns the playback state
// type; the Player owns the instance and the authoritative action clock.
//
// ANIM-2 A10.1 — Player-owned playback instance.
//
// One instance per Player. `Play()` is the production entry: the caller supplies
// the authoritative clip, integer frame (from PlayerActionState) and base yaw,
// and receives the single FinalPose for this tick. It samples the pose, advances
// the cross-clip transition at most once and stores the result; it never
// advances the action clock itself. `Pose()`/`BaseAngle()` are pure reads that
// change nothing.
//
// M2: `PlayAtTime()` is the same entry for motion-driven playback. Ordinary
// locomotion must sample at the rate the Player actually moves, so the caller
// supplies a continuous clip time instead of an integer action frame. The
// action clock stays the only action-time authority; clip time is derived from
// it (actions) or from the achieved motion (locomotion).
//
// `Tick()`/`AdvanceCursor()` are the self-advancing test helpers. They share the
// sampling core but are not part of the production interface, so they cannot
// become a second time authority.
//
// ANIM-2 A12.4: the advancing bodies live in src/animation/playback.cpp.

namespace football::animation {

struct PlaybackState {
  AnimationId clip_id = -1;
  // Integer frame derived from `frame_time`; kept for audit and reporting.
  int frame = 0;
  // M2: continuous clip time in frames. Motion-driven playback advances this by
  // the achieved/reference speed ratio, so a stride is sampled at the rate the
  // Player actually moves instead of one baked frame per tick.
  float frame_time = 0.0f;
  bool have_pose = false;
  PoseFrame final_pose;
  float final_base_angle = 0.0f;
  // M3: the pose a clip switch blends from. The Player publishes its Final Pose
  // here once per tick, so a transition starts from what the body actually
  // looked like - not from the raw animation sample the Player then corrects.
  PoseFrame blend_source_pose;
  float blend_source_base_angle = 0.0f;
  bool blend_source_valid = false;
  AnimationTransitionState transition;

  void Reset();

  // The Player owns this value: it is the Final Pose of the previous tick.
  void SetBlendSource(const PoseFrame& pose, float base_angle);

  // Production: one real playback update for this tick. A clip change blends
  // from the published blend source over `blend_ticks`.
  const PoseFrame& Play(const AnimationClip& clip, int at_frame, float base_angle,
                        int blend_ticks);

  // M2: same contract, but sampled at a continuous clip time. The pose is
  // SLERPed between baked frames; `frame` is the rounded time for audit.
  const PoseFrame& PlayAtTime(const AnimationClip& clip, float at_frame_time,
                              float base_angle, int blend_ticks);

  // The blended animation pose of this tick. The Player's Final Pose is one
  // stage downstream of this value; read that one through the execution.
  const PoseFrame& Pose() const { return final_pose; }
  float BaseAngle() const { return final_base_angle; }

  // Test-only: self-advancing playback. Advances the internal cursor one frame
  // per call; never used by production.
  const PoseFrame& Tick(const AnimationClip& clip, int blend_ticks);

  // Test-only: advance the internal cursor for the next self-advancing Tick.
  void AdvanceCursor(const AnimationClip& clip);

 private:
  // The sampling core shared by the production and test entries.
  const PoseFrame& Apply(const AnimationClip& clip, float at_frame_time,
                         float base_angle, int blend_ticks);
};

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_PLAYBACK_HPP
