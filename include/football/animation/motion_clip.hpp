#ifndef FOOTBALL_ANIMATION_MOTION_CLIP_HPP
#define FOOTBALL_ANIMATION_MOTION_CLIP_HPP

#include <cmath>
#include <cstddef>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/foundation/math/scalar.hpp"
#include "football/foundation/math/vector3.hpp"

// Motion reference data derived from an animation clip, in the clip's own local
// frame. This is *reference*, not player state: it owns no world position and no
// pose. Animation keeps producing the pose; Motion decides what to do with the
// displacement. The source AnimationClip is never modified, so animation
// selection, metadata and touch records are untouched.
//
// Z is deliberately excluded. The baked root z is the pelvis bob and is still
// applied as part of the animation root pose; adding it here as well would double
// it. Real vertical motion (jumping, diving) becomes an explicit vertical state
// later, not a second copy of the animation bob.
//
// Time semantics (one vocabulary, no ambiguity):
//   frame index      which animation frame: 0 .. size()-1
//   frame time       continuous clip time measured in frames: 0.0 .. size()-1
//   playback rate    frames of clip time advanced per unit of simulation time
//   motion delta     the local displacement over one interval of frame time
// `SampleDisplacement()` is the only operation production code needs: it answers
// "how far did the reference move between these two clip times", so a caller can
// scale it by a playback rate and split it across ticks without drifting.
struct MotionFrame {
  football::math::Vector3 local_displacement = football::math::Vector3(0);
};

struct MotionClip {
  // One entry per AnimationClip frame. frames[0] is always zero; frames[i] is the
  // local XY displacement between clip frames i-1 and i, so the frames are
  // tick-shaped and can be sampled without re-reading root_positions.
  std::vector<MotionFrame> frames;

  bool empty() const { return frames.empty(); }
  std::size_t size() const { return frames.size(); }
  // Last valid frame index, i.e. the clip's length in frame time.
  float last_frame_time() const {
    return frames.empty() ? 0.0f : static_cast<float>(frames.size() - 1);
  }
};

// Extracts the per-frame local XY displacement from the baked root motion.
inline MotionClip ExtractMotionClip(const AnimationClip& clip) {
  MotionClip motion;
  motion.frames.resize(clip.root_positions.size());
  for (std::size_t frame = 1; frame < clip.root_positions.size(); ++frame) {
    const football::math::Vector3 delta =
        clip.root_positions[frame] - clip.root_positions[frame - 1];
    motion.frames[frame].local_displacement =
        football::math::Vector3(delta.coords[0], delta.coords[1], 0.0f);
  }
  return motion;
}

// Cumulative local reference position at an integer frame index, measured from
// frame 0. frame_index 0 is the origin and size()-1 is the clip's total travel.
// An index past the last frame clamps: a finished clip does not keep accumulating
// its last displacement.
inline football::math::Vector3 ReferencePosition(const MotionClip& clip,
                                                std::size_t frame_index) {
  if (clip.frames.empty()) return football::math::Vector3(0);
  const std::size_t last = clip.frames.size() - 1;
  const std::size_t index = frame_index < last ? frame_index : last;
  football::math::Vector3 position(0);
  for (std::size_t i = 1; i <= index; ++i) {
    position += clip.frames[i].local_displacement;
  }
  return position;
}

// Cumulative local reference position at a fractional frame time, linearly
// interpolated between frame samples. Out-of-range times clamp to the ends, so
// playing past the last frame holds the final reference position instead of
// repeating its displacement.
inline football::math::Vector3 ReferencePositionAt(const MotionClip& clip,
                                                  float frame_time) {
  if (clip.frames.empty()) return football::math::Vector3(0);
  const float clamped =
      football::math::clamp(frame_time, 0.0f, clip.last_frame_time());
  const auto lower = static_cast<std::size_t>(std::floor(clamped));
  const float fraction = clamped - static_cast<float>(lower);
  football::math::Vector3 position(0);
  for (std::size_t i = 1; i <= lower; ++i) {
    position += clip.frames[i].local_displacement;
  }
  if (fraction > 0.0f && lower + 1 < clip.frames.size()) {
    position += clip.frames[lower + 1].local_displacement * fraction;
  }
  return position;
}

// Local displacement over [start_time, end_time] of frame time. Additive by
// construction, so splitting an interval into several ticks and summing the parts
// gives the same total; a playback rate is applied by the caller scaling the
// times, never by scaling the result at different call sites.
inline football::math::Vector3 SampleDisplacement(const MotionClip& clip,
                                                 float start_time,
                                                 float end_time) {
  return ReferencePositionAt(clip, end_time) -
         ReferencePositionAt(clip, start_time);
}

#endif  // FOOTBALL_ANIMATION_MOTION_CLIP_HPP
