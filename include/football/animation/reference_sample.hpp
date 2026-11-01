#pragma once

#include "football/animation/clip.hpp"
#include "football/foundation/math/vector3.hpp"

// RM-2: one reference sample over one interval of the clip's own timeline.
//
// Root delta and pose are read from the same GLB track and the same interval:
//
//   root_delta = R(t1) - R(t0)   (local clip frame)
//   pose       = P(t1)
//
// For a cyclic clip the time is unwrapped before the difference is measured, so
// crossing the loop seam advances by the cycle's travel instead of jumping back
// to the first sample. The pose is always sampled at the wrapped time, because
// the skeleton repeats while the root accumulates.
//
// This is *reference* data in the clip's local frame. It owns no Player, no
// world state and no decision; the caller rotates/scales the delta for its body
// and commits it, and a capability constraint may still reject it.
struct ReferenceAnimationSample {
  // Local root displacement over [current_time, next_time].
  football::math::Vector3 root_delta;
  // Skeleton at `pose_time` (the wrapped next_time).
  PoseFrame pose;

  // Wrapped clip time the pose was sampled at, in frames.
  float pose_time = 0.0f;
  // Unwrapped clip time after this interval; carry it as the next current_time.
  float continuous_time = 0.0f;
  // The interval crossed at least one loop seam.
  bool looped = false;
};

// Pure and deterministic. `current_time` and `next_time` are continuous clip
// frame times (10 ms per frame at playback rate 1) and must satisfy
// next_time >= current_time. A non-looping clip clamps to its last sample; a
// looping clip measures the root on the unwrapped cycle and wraps the pose.
ReferenceAnimationSample SampleAnimationStep(const AnimationClip& clip,
                                             float current_time,
                                             float next_time);
