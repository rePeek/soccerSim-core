//
//  player_body_collider_motion.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_BODY_COLLIDER_MOTION
#define _HPP_PLAYER_BODY_COLLIDER_MOTION

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <limits>

#include "football/ball/collider.hpp"
#include "sim/player/player_body_collider.hpp"

// The three coarse passive body shapes intentionally match PlayerBodyCollider.
// They are not action volumes and they do not describe the posed skeleton.
enum class PlayerBodyPart : std::uint8_t {
  UpperBody = 0,
  LowerBody = 1,
  Head = 2,
};

inline constexpr std::size_t kPlayerBodyPartCount = 3;
inline constexpr football::ball::ColliderId kFirstDynamicBodyColliderId =
    0x80000000u;

// Dynamic ids occupy a range disjoint from the pitch's 1--7 ids. The slot is
// a Simulation-owned fixed match slot, never a roster traversal position or a
// player name. Keeping the encoding here makes contact identities stable when
// collider construction is later parallelized or reordered.
inline football::ball::ColliderId PlayerBodyColliderId(
    std::uint32_t player_slot, PlayerBodyPart part) {
  constexpr std::uint64_t kParts = kPlayerBodyPartCount;
  const std::uint64_t id =
      static_cast<std::uint64_t>(kFirstDynamicBodyColliderId) +
      static_cast<std::uint64_t>(player_slot) * kParts +
      static_cast<std::uint8_t>(part);
  if (id > std::numeric_limits<football::ball::ColliderId>::max()) {
    throw std::out_of_range("player body collider slot exceeds dynamic id range");
  }
  return static_cast<football::ball::ColliderId>(id);
}

struct PlayerBodyColliderMotionIds {
  std::array<football::ball::ColliderId, kPlayerBodyPartCount> values;

  football::ball::ColliderId operator[](PlayerBodyPart part) const {
    return values[static_cast<std::size_t>(part)];
  }
};

inline PlayerBodyColliderMotionIds PlayerBodyColliderIdsForSlot(
    std::uint32_t player_slot) {
  return {{PlayerBodyColliderId(player_slot, PlayerBodyPart::UpperBody),
           PlayerBodyColliderId(player_slot, PlayerBodyPart::LowerBody),
           PlayerBodyColliderId(player_slot, PlayerBodyPart::Head)}};
}

inline football::ball::Capsule BodyVolumeCapsule(const BodyVolume& volume) {
  return {volume.center - blunted::Vector3(0.0f, 0.0f, volume.halfHeight),
          volume.center + blunted::Vector3(0.0f, 0.0f, volume.halfHeight),
          volume.radius};
}

// Fills three motions in UpperBody, LowerBody, Head order. Callers establish
// ids before this pure geometry conversion; geometry itself has no Player,
// Team, animation, RNG, or Simulation dependency.
inline void BuildBodyColliderMotions(
    const PlayerKinematicState& start, const PlayerKinematicState& end,
    std::span<football::ball::ColliderMotion> output,
    const PlayerBodyColliderParameters& parameters =
        PlayerBodyColliderParameters()) {
  if (output.size() != kPlayerBodyPartCount) {
    throw std::invalid_argument("body collider motion output must contain three shapes");
  }

  const PlayerBodyCollider start_body = BuildBodyCollider(start, parameters);
  const PlayerBodyCollider end_body = BuildBodyCollider(end, parameters);
  const std::array<const BodyVolume*, kPlayerBodyPartCount> starts =
      start_body.GetVolumes();
  const std::array<const BodyVolume*, kPlayerBodyPartCount> ends =
      end_body.GetVolumes();
  for (std::size_t i = 0; i < kPlayerBodyPartCount; ++i) {
    if (i == static_cast<std::size_t>(PlayerBodyPart::Head)) {
      output[i].start = football::ball::Sphere{starts[i]->center, starts[i]->radius};
      output[i].end = football::ball::Sphere{ends[i]->center, ends[i]->radius};
    } else {
      output[i].start = BodyVolumeCapsule(*starts[i]);
      output[i].end = BodyVolumeCapsule(*ends[i]);
    }
  }
}

inline void BuildBodyColliderMotions(
    const PlayerKinematicState& start, const PlayerKinematicState& end,
    const PlayerBodyColliderMotionIds& ids,
    std::span<football::ball::ColliderMotion> output,
    const PlayerBodyColliderParameters& parameters =
        PlayerBodyColliderParameters()) {
  if (output.size() != kPlayerBodyPartCount) {
    throw std::invalid_argument("body collider motion output must contain three shapes");
  }
  for (std::size_t i = 0; i < kPlayerBodyPartCount; ++i) output[i].id = ids.values[i];
  BuildBodyColliderMotions(start, end, output, parameters);
}

#endif  // _HPP_PLAYER_BODY_COLLIDER_MOTION
