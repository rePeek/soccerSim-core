#ifndef FOOTBALL_ANIMATION_POSE_BLEND_HPP
#define FOOTBALL_ANIMATION_POSE_BLEND_HPP

#include "football/animation/clip.hpp"
#include "football/foundation/math/quaternion.hpp"

// Pure pose blend kernel (LOCO-2.6a). No Humanoid, Simulation, Ball or RNG.
//
// weight 0 returns source exactly, weight 1 returns target exactly, and in
// between the 14 joint orientations SLERP on the shortest rotation path while
// positions (the root Z pelvis bob is the only FK-consumed position) lerp
// linearly. Root XY remains animation reference data and must never be
// integrated into the Player world position.

inline PoseFrame BlendPose(const PoseFrame& source, const PoseFrame& target,
                           float weight) {
  if (weight <= 0.0f) return source;
  if (weight >= 1.0f) return target;

  PoseFrame out;
  for (std::size_t b = 0; b < kBodyPartCount; ++b) {
    // GetSlerped already negates the target for the shortest path and returns
    // a unit quaternion for interior weights; normalise guards the 180-degree
    // fallback.
    out.orientations[b] =
        source.orientations[b].GetSlerped(weight, target.orientations[b])
            .GetNormalized();
    out.positions[b] = source.positions[b] * (1.0f - weight) +
                       target.positions[b] * weight;
  }
  return out;
}

#endif  // FOOTBALL_ANIMATION_POSE_BLEND_HPP