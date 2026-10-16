// ANIM-1 A1 / ANIM-2 A12.4 — the pure pose sampling, moved verbatim out of
// pose_sampling.hpp. Same clamping, SLERP/lerp expressions and integer-frame
// fast path.

#include "football/animation/pose_sampling.hpp"

#include <cmath>

#include "football/foundation/math/scalar.hpp"

float PoseFrameTimeClamped(const AnimationClip& clip, float frame_time) {
  if (clip.poses.empty()) return 0.0f;
  return football::math::clamp(frame_time, 0.0f,
                               static_cast<float>(clip.poses.size() - 1));
}

PoseFrame SamplePose(const AnimationClip& clip, float frame_time) {
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
