#ifndef FOOTBALL_ANIMATION_MOTION_CLIP_HPP
#define FOOTBALL_ANIMATION_MOTION_CLIP_HPP

#include <cstddef>
#include <vector>

#include "football/animation/clip.hpp"
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

// Cumulative local reference position at `frame`, i.e. the baked root motion
// reconstructed from the extracted displacements. Exists so the extraction can be
// verified against its source and so Motion can bound a clip's total travel.
inline football::math::Vector3 ReferencePosition(const MotionClip& clip,
                                                std::size_t frame) {
  football::math::Vector3 position(0);
  const std::size_t last = frame < clip.frames.size() ? frame : clip.frames.size();
  for (std::size_t i = 1; i < last; ++i) {
    position += clip.frames[i].local_displacement;
  }
  return position;
}

#endif  // FOOTBALL_ANIMATION_MOTION_CLIP_HPP
