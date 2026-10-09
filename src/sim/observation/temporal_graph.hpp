#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "sim/observation/snapshot_history.hpp"

namespace football::sim::observation {

// Value-only graph input for a future Temporal GNN, not a model/training runtime.
// Slot zero is the ball, then the fixed metadata player order at each frame.
struct TemporalGraphNode {
  std::size_t frame_index = 0;
  std::optional<model::PlayerId> player_id; // nullopt = ball
  blunted::Vector3 position{0};
  blunted::Vector3 velocity{0};
  blunted::Vector3 angular_velocity{0}; // ball only
  blunted::Vector3 facing{0};
  blunted::Vector3 body_facing{0};
  AnimationId animation_id = -1;
  std::uint32_t animation_frame = 0;
  bool active = false;
  bool has_possession = false;
};

enum class TemporalEdgeKind { Spatial, Temporal };
struct TemporalGraphEdge {
  std::size_t source = 0;
  std::size_t target = 0;
  TemporalEdgeKind kind = TemporalEdgeKind::Spatial;
  blunted::Vector3 displacement{0};
  TickSpan elapsed{};
};
struct TemporalGraph {
  std::vector<SnapshotStamp> frames;
  std::vector<TemporalGraphNode> nodes;
  std::vector<TemporalGraphEdge> edges;
};
struct GraphBuildOptions {
  float spatial_radius = 15.0f; // metres; engineering feature policy, not physics
};

class GraphBuilder {
 public:
  // Directed radius edges within a frame, forward time edges for the same slot.
  // Inactive slots remain masked nodes but have no edges. No temporal edge crosses
  // a reset, skipped step/tick, or duplicate tick. Inputs are owning/copied values
  // or synchronous ring borrows; Build never consults live actors or disk.
  static TemporalGraph Build(std::span<const SnapshotRecord> window,
                             const SnapshotMetadata& metadata,
                             GraphBuildOptions options = {});
  // Explicit owning copy from the same live history used by Event/Referee queries.
  // Insufficient retained samples returns nullopt, never reads older data from disk.
  static std::optional<TemporalGraph> BuildLatest(const SnapshotHistory& history,
                                                 const SnapshotMetadata& metadata,
                                                 std::size_t count,
                                                 GraphBuildOptions options = {});
};

}  // namespace football::sim::observation
