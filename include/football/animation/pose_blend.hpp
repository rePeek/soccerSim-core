#ifndef FOOTBALL_ANIMATION_POSE_BLEND_HPP
#define FOOTBALL_ANIMATION_POSE_BLEND_HPP

#include "football/animation/clip.hpp"

// Pure pose blend kernel (LOCO-2.6a). No Humanoid, Simulation, Ball or RNG.
//
// weight 0 returns source exactly, weight 1 returns target exactly, and in
// between the 14 joint orientations SLERP on the shortest rotation path while
// positions (the root Z pelvis bob is the only FK-consumed position) lerp
// linearly. Root XY remains animation reference data and must never be
// integrated into the Player world position.
//
// ANIM-2 A12.4: the body lives in src/animation/pose_blend.cpp.

PoseFrame BlendPose(const PoseFrame& source, const PoseFrame& target,
                    float weight);

#endif  // FOOTBALL_ANIMATION_POSE_BLEND_HPP
