// RM-2: unified reference sampling for root motion and pose.
#include "football/animation/reference_sample.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "football/animation/pose_sampling.hpp"

using football::math::Vector3;

Vector3 SampleRootPosition(std::span<const Vector3> roots, float time) {
  if (roots.empty()) return Vector3(0);
  const float last = static_cast<float>(roots.size() - 1);
  const float clamped = std::clamp(time, 0.0f, last);
  const auto lower = static_cast<std::size_t>(std::floor(clamped));
  const float fraction = clamped - static_cast<float>(lower);
  Vector3 position = roots[lower] - roots.front();
  if (fraction > 0.0f && lower + 1 < roots.size()) {
    position +=
        (roots[lower + 1] - roots[lower]) * fraction;
  }
  return position;
}

namespace {

float CyclePeriod(const AnimationClip& clip) {
  const float samples = static_cast<float>(clip.root_positions.size());
  return std::max(samples - 1.0f, 1.0f);
}

// Root position on the unwrapped timeline. `period` frames are one cycle, and
// frame `period` is the same gait instant as frame 0 but has advanced by the
// cycle's total travel.
Vector3 LoopRootAt(const AnimationClip& clip, float time, float period,
                   const Vector3& total) {
  if (time >= 0.0f) {
    const float cycles = std::floor(time / period);
    const float local = time - cycles * period;
    return total * cycles + SampleRootPosition(clip.root_positions, local);
  }
  // A negative time is before the clip starts; clamp instead of inventing a
  // reverse cycle.
  return SampleRootPosition(clip.root_positions, 0.0f);
}

}  // namespace

ReferenceAnimationSample SampleAnimationStep(const AnimationClip& clip,
                                             float current_time,
                                             float next_time) {
  ReferenceAnimationSample sample;
  if (clip.poses.empty() || clip.root_positions.size() < 2) {
    return sample;
  }
  const float period = CyclePeriod(clip);
  if (clip.looping) {
    const Vector3 total = clip.root_positions[static_cast<std::size_t>(period)] -
                          clip.root_positions.front();
    const float from = std::max(current_time, 0.0f);
    const float to = std::max(next_time, from);
    sample.root_delta = LoopRootAt(clip, to, period, total) -
                        LoopRootAt(clip, from, period, total);
    const auto from_cycle =
        static_cast<std::int64_t>(std::floor(from / period));
    const auto to_cycle =
        static_cast<std::int64_t>(std::floor(to / period));
    sample.pose_time = to - static_cast<float>(to_cycle) * period;
    sample.continuous_time = to;
    sample.looped = to_cycle > from_cycle;
    sample.cycle_index = to_cycle;
  } else {
    const float last = static_cast<float>(clip.root_positions.size() - 1);
    const float from = std::clamp(current_time, 0.0f, last);
    const float to = std::clamp(std::max(next_time, from), 0.0f, last);
    sample.root_delta =
        SampleRootPosition(clip.root_positions, to) -
        SampleRootPosition(clip.root_positions, from);
    sample.pose_time = to;
    sample.continuous_time = to;
    sample.looped = false;
  }
  sample.pose = SamplePose(clip, sample.pose_time);
  return sample;
}
