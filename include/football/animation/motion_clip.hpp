#pragma once

#include <cstddef>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/foundation/math/vector3.hpp"

// Discrete XY reference contract: one delta per source frame, accumulated when
// sampled. It owns neither pose nor world state and never modifies its source.
// Root Z remains in the pose as pelvis bob, not in these displacements.
//
// This interface currently has no production callers. It is retained as the
// tested discrete trajectory contract, not an alternative runtime sampler.
// Use SampleAnimationStep for paired runtime root/pose sampling.
//
// Unlike paired sampling, MotionClip always clamps (even for a looping source),
// permits reversed intervals, needs no pose track and sums deltas rather than
// subtracting raw root samples. Do not alias it to the paired sampler: that
// would change loop/boundary semantics and floating-point accumulation.
// Times are continuous frame units (one frame = 10 ms at native rate).

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
MotionClip ExtractMotionClip(const AnimationClip& clip);

// Cumulative local reference position at an integer frame index, measured from
// frame 0. frame_index 0 is the origin and size()-1 is the clip's total travel.
// An index past the last frame clamps: a finished clip does not keep accumulating
// its last displacement.
football::math::Vector3 ReferencePosition(const MotionClip& clip,
                                          std::size_t frame_index);

// Cumulative local reference position at a fractional frame time, linearly
// interpolated between frame samples. Out-of-range times clamp to the ends, so
// playing past the last frame holds the final reference position instead of
// repeating its displacement.
football::math::Vector3 ReferencePositionAt(const MotionClip& clip,
                                            float frame_time);

// Local displacement over [start_time, end_time] of frame time. Additive by
// construction, so splitting an interval into several ticks and summing the parts
// gives the same total; a playback rate is applied by the caller scaling the
// times, never by scaling the result at different call sites.
football::math::Vector3 SampleDisplacement(const MotionClip& clip,
                                           float start_time, float end_time);
