// Provisional low-action geometry, SHADOW ONLY. Not the legacy body collider.
#ifndef FOOTBALL_PLAYER_BODY_POSE_SHADOW_HPP
#define FOOTBALL_PLAYER_BODY_POSE_SHADOW_HPP
#include <type_traits>
#include "sim/player/player_action.hpp"
#include "sim/player/player_body_collider_motion.hpp"

// Axis must be in the same frame as positions. PlayerKinematicState::Mirror
// intentionally does NOT mirror bodyFacing; its callers must convert explicitly.
inline void BuildShadowPosedBodyMotions(
    const PlayerKinematicState& start, const PlayerKinematicState& end,
    e_FunctionType action, blunted::Vector3 axis,
    const PlayerBodyColliderMotionIds& ids,
    std::span<football::ball::ColliderMotion> output) {
  BuildBodyColliderMotions(start, end, ids, output);
  if (action != e_FunctionType_Sliding && action != e_FunctionType_Trip) return;
  axis = axis.Get2D().GetNormalized(blunted::Vector3(0, -1, 0));
  const bool sliding = action == e_FunctionType_Sliding;
  const auto shapes = [&](const PlayerKinematicState& k) {
    const auto origin = k.position.Get2D();
    const auto at = [&](float offset, float height) {
      return origin + axis * offset + blunted::Vector3(0, 0, height);
    };
    return std::array<football::ball::ColliderShape, 3>{
        football::ball::Capsule{at(sliding ? -.55f : 0, .24f), at(sliding ? .05f : .55f, .24f), .20f},
        football::ball::Capsule{at(sliding ? .1f : -.7f, .16f), at(sliding ? .9f : -.05f, .16f), .13f},
        football::ball::Sphere{at(sliding ? -.75f : .8f, .25f), .12f}};
  };
  const auto starts = shapes(start), ends = shapes(end);
  for (std::size_t part = 0; part < output.size(); ++part) {
    output[part].start = starts[part];
    output[part].end = ends[part];
  }
  // Freeze pose/axis for this 10ms sweep: kernel capsules are translational.
  // No guessed rotating CCD or interpolated stand/fall transition is introduced.
}

// Signed initial ball-surface gap, for episodes independent of impact impulses.
// Tangency/resting contact is geometric even if SweepBall rejects an exit root.
inline float BodyShadowGap(const blunted::Vector3& center, float radius,
                           const football::ball::ColliderShape& shape) {
  return std::visit([&](const auto& s) {
    using T = std::decay_t<decltype(s)>;
    if constexpr (std::is_same_v<T, football::ball::Sphere>) {
      return (center - s.center).GetLength() - radius - s.radius;
    } else if constexpr (std::is_same_v<T, football::ball::Capsule>) {
      const auto ab = s.b - s.a;
      const auto length2 = ab.GetDotProduct(ab);
      const float u = length2 > 1e-8f ? blunted::clamp((center - s.a).GetDotProduct(ab) / length2, 0.0f, 1.0f) : 0;
      return (center - s.a - ab * u).GetLength() - radius - s.radius;
    } else {
      return (center - s.point).GetDotProduct(s.normal.GetNormalized({0, 0, 1})) - radius;
    }
  }, shape);
}
#endif
