// LOCO-2.6a / ANIM-2 A12.4 — the pure pose blend kernel, moved verbatim out of
// pose_blend.hpp. Same SLERP/lerp expressions and the same 0/1 fast paths.

#include "football/animation/pose_blend.hpp"

#include "football/foundation/math/quaternion.hpp"

PoseFrame BlendPose(const PoseFrame& source, const PoseFrame& target,
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
