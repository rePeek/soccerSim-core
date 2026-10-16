// ANIM-2 A12.4 — the motion-clip extraction and reference sampling, moved
// verbatim out of motion_clip.hpp. Same delta accumulation and interpolation.

#include "football/animation/motion_clip.hpp"

#include <cmath>

#include "football/foundation/math/scalar.hpp"

MotionClip ExtractMotionClip(const AnimationClip& clip) {
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

football::math::Vector3 ReferencePosition(const MotionClip& clip,
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

football::math::Vector3 ReferencePositionAt(const MotionClip& clip,
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

football::math::Vector3 SampleDisplacement(const MotionClip& clip,
                                           float start_time, float end_time) {
  return ReferencePositionAt(clip, end_time) -
         ReferencePositionAt(clip, start_time);
}
