#ifndef FOOTBALL_ANIMATION_POSE_SAMPLING_HPP
#define FOOTBALL_ANIMATION_POSE_SAMPLING_HPP

#include <cmath>

#include "football/animation/clip.hpp"
#include "football/foundation/math/scalar.hpp"

// Pure pose sampling (ANIM-1 A1). Given an immutable clip and a frame time,
// return the skeleton pose. An integer frame time reproduces the exact baked
// frame; a fractional time SLERPs the joint rotations (shortest path) and lerps
// the positions (root Z). Out-of-range times clamp to the ends. No Player, Ball,
// Simulation or RNG dependency. Root XY stays clip reference data and is never a
// world displacement.

inline float PoseFrameTimeClamped(const AnimationClip& clip, float frame_time) {
  if (clip.poses.empty()) return 0.0f;
  return football::math::clamp(frame_time, 0.0f,
                               static_cast<float>(clip.poses.size() - 1));
}

inline PoseFrame SamplePose(const AnimationClip& clip, float frame_time) {
  PoseFrame out;
  if (clip.poses.empty()) return out;

  const float t = PoseFrameTimeClamped(clip, frame_time);
  const auto lower = static_cast<std::size_t>(std::floor(t));
  auto upper = static_cast<std::size_t>(std::ceil(t));
  if (upper >= clip.poses.size()) upper = lower;
  const float fraction = t - static_cast<float>(lower);

  for (std::size_t b = 0; b < kBodyPartCount; ++b) {
    if (lower == upper || fraction <= 0.0f) {
      out.orientations[b] = clip.poses[lower].orientations[b];
      out.positions[b] = clip.poses[lower].positions[b];
      continue;
    }
    const auto from = clip.poses[lower].orientations[b];
    auto to = clip.poses[upper].orientations[b];
    to.MakeSameNeighborhood(from);  // shortest path: sign alignment once
    out.orientations[b] = from.GetSlerped(fraction, to).GetNormalized();

    const auto& p0 = clip.poses[lower].positions[b];
    const auto& p1 = clip.poses[upper].positions[b];
    out.positions[b] = p0 * (1.0f - fraction) + p1 * fraction;
  }
  return out;
}

#endif  // FOOTBALL_ANIMATION_POSE_SAMPLING_HPP
