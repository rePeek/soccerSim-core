#pragma once

#include <cstdint>
#include <vector>

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/player.hpp"
#include "model/pitch.hpp"
#include "sim/animation/types.hpp"

namespace football::sim::observation {

// Dynamic values only, in the fixed home-pitch frame shared with WorldState.
// Animation identity/frame describe a pose; this is not a simulation checkpoint.
struct PlayerSnapshot {
  blunted::Vector3 position{0};
  blunted::Vector3 velocity{0};
  blunted::Vector3 facing{1, 0, 0};
  blunted::Vector3 body_facing{1, 0, 0};
  AnimationId animation_id = -1;
  std::uint32_t frame = 0;
  bool active = false;
  bool has_possession = false;
};

struct BallSnapshot {
  blunted::Vector3 position{0};
  blunted::Vector3 velocity{0};
  blunted::Vector3 angular_velocity{0};
};

struct Snapshot {
  BallSnapshot ball;
  std::vector<PlayerSnapshot> players;
};

// Match-wide slot order: never compact inactive players or reuse a slot for a
// different identity. The producer establishes this table when starting a match.
struct SnapshotMetadata {
  std::vector<football::model::PlayerId> player_ids;
  football::model::Pitch pitch;
  // FNV-1a-64 over the exact loaded .simanim bytes (not a cryptographic hash).
  std::uint64_t animation_library_hash = 0;
};

struct SnapshotStamp {
  std::uint64_t step_index = 0;
  football::sim::Tick timeline_tick{};
  std::uint64_t generation = 0;
};

struct SnapshotRecord {
  SnapshotStamp stamp;
  Snapshot snapshot;
};

}  // namespace football::sim::observation
