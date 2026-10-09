#include "sim/observation/temporal_graph.hpp"

#include <cmath>
#include <set>
#include <stdexcept>

namespace football::sim::observation {
namespace {
void RequireFinite(const blunted::Vector3& value) {
  for (float coord : value.coords) {
    if (!std::isfinite(coord)) throw std::invalid_argument("graph requires finite snapshot vectors");
  }
}
}

TemporalGraph GraphBuilder::Build(std::span<const SnapshotRecord> window,
                                  const SnapshotMetadata& metadata, GraphBuildOptions options) {
  if (!std::isfinite(options.spatial_radius) || options.spatial_radius <= 0)
    throw std::invalid_argument("graph spatial radius must be finite and positive");
  std::set<model::PlayerId> ids;
  for (auto id : metadata.player_ids) {
    if (id == model::kInvalidPlayerId || !ids.insert(id).second)
      throw std::invalid_argument("graph requires unique valid player slots");
  }
  TemporalGraph graph;
  const auto slots = metadata.player_ids.size() + 1;
  if (window.size() > graph.nodes.max_size() / slots)
    throw std::length_error("graph window is too large");
  graph.frames.reserve(window.size());
  graph.nodes.reserve(window.size() * slots);
  for (std::size_t f = 0; f < window.size(); ++f) {
    const auto& record = window[f];
    if (record.snapshot.players.size() != metadata.player_ids.size())
      throw std::invalid_argument("graph player slot count mismatch");
    if (f != 0) {
      const auto& previous = window[f - 1].stamp;
      if (record.stamp.step_index <= previous.step_index || record.stamp.timeline_tick < previous.timeline_tick ||
          record.stamp.generation < previous.generation)
        throw std::invalid_argument("graph window stamps must be ordered");
    }
    graph.frames.push_back(record.stamp);
    const auto& ball = record.snapshot.ball;
    RequireFinite(ball.position); RequireFinite(ball.velocity); RequireFinite(ball.angular_velocity);
    TemporalGraphNode ball_node;
    ball_node.frame_index = f;
    ball_node.position = ball.position;
    ball_node.velocity = ball.velocity;
    ball_node.angular_velocity = ball.angular_velocity;
    ball_node.active = true;
    graph.nodes.push_back(ball_node);
    for (std::size_t p = 0; p < metadata.player_ids.size(); ++p) {
      const auto& player = record.snapshot.players[p];
      RequireFinite(player.position); RequireFinite(player.velocity);
      RequireFinite(player.facing); RequireFinite(player.body_facing);
      TemporalGraphNode node;
      node.frame_index = f;
      node.player_id = metadata.player_ids[p];
      node.position = player.position;
      node.velocity = player.velocity;
      node.facing = player.facing;
      node.body_facing = player.body_facing;
      node.animation_id = player.animation_id;
      node.animation_frame = player.frame;
      node.active = player.active;
      node.has_possession = player.has_possession;
      graph.nodes.push_back(node);
    }
    const auto base = f * slots;
    for (std::size_t i = 0; i < slots; ++i) {
      if (!graph.nodes[base + i].active) continue;
      for (std::size_t j = i + 1; j < slots; ++j) {
        if (!graph.nodes[base + j].active) continue;
        const auto delta = graph.nodes[base + j].position - graph.nodes[base + i].position;
        // Use double for the radius comparison to avoid squaring a large float.
        const double distance_sq = double(delta.coords[0]) * delta.coords[0] +
            double(delta.coords[1]) * delta.coords[1] + double(delta.coords[2]) * delta.coords[2];
        if (distance_sq > double(options.spatial_radius) * options.spatial_radius) continue;
        graph.edges.push_back({base + i, base + j, TemporalEdgeKind::Spatial, delta, {}});
        graph.edges.push_back({base + j, base + i, TemporalEdgeKind::Spatial, -delta, {}});
      }
    }
    if (f == 0) continue;
    const auto& previous = window[f - 1].stamp;
    if (record.stamp.generation != previous.generation || record.stamp.step_index - previous.step_index != 1 ||
        record.stamp.timeline_tick.value - previous.timeline_tick.value != 1) continue;
    for (std::size_t p = 0; p < slots; ++p) {
      const auto old_node = base - slots + p, new_node = base + p;
      if (!graph.nodes[old_node].active || !graph.nodes[new_node].active) continue;
      graph.edges.push_back({old_node, new_node, TemporalEdgeKind::Temporal,
          graph.nodes[new_node].position - graph.nodes[old_node].position, TickSpan{1}});
    }
  }
  return graph;
}

std::optional<TemporalGraph> GraphBuilder::BuildLatest(const SnapshotHistory& history,
                                                      const SnapshotMetadata& metadata,
                                                      std::size_t count, GraphBuildOptions options) {
  std::vector<SnapshotRecord> window;
  if (!history.CopyLatest(count, window)) return std::nullopt;
  return Build(window, metadata, options);
}

}  // namespace football::sim::observation
