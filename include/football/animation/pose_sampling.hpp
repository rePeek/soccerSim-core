#ifndef FOOTBALL_ANIMATION_POSE_SAMPLING_HPP
#define FOOTBALL_ANIMATION_POSE_SAMPLING_HPP

#include "football/animation/clip.hpp"

// Pure pose sampling (ANIM-1 A1). Given an immutable clip and a frame time,
// return the skeleton pose. An integer frame time reproduces the exact baked
// frame; a fractional time SLERPs the joint rotations (shortest path) and lerps
// the positions (root Z). Out-of-range times clamp to the ends. No Player, Ball,
// Simulation or RNG dependency. Root XY stays clip reference data and is never a
// world displacement.
//
// ANIM-2 A12.4: the bodies live in src/animation/pose_sampling.cpp.

float PoseFrameTimeClamped(const AnimationClip& clip, float frame_time);

PoseFrame SamplePose(const AnimationClip& clip, float frame_time);

#endif  // FOOTBALL_ANIMATION_POSE_SAMPLING_HPP
