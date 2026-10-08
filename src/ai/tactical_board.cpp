#include "ai/tactical_board.hpp"

#include <algorithm>

namespace football::ai {
namespace {
using blunted::Vector3;

Vector3 RoleAnchor(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK: case e_PlayerRole_CB: return Vector3(-1.0, 0.0, 0);
    case e_PlayerRole_LB: return Vector3(-0.8, 0.8, 0);
    case e_PlayerRole_RB: return Vector3(-0.8, -0.8, 0);
    case e_PlayerRole_DM: return Vector3(-0.5, 0.0, 0);
    case e_PlayerRole_LM: return Vector3(0.0, 1.0, 0);
    case e_PlayerRole_RM: return Vector3(0.0, -1.0, 0);
    case e_PlayerRole_AM: return Vector3(0.5, 0.0, 0);
    case e_PlayerRole_CF: return Vector3(1.0, 0.0, 0);
    default: return Vector3(0.0, 0.0, 0);
  }
}

PlannedPlayerRole PlannedRole(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK: return PlannedPlayerRole::Goalkeeper;
    case e_PlayerRole_CB: case e_PlayerRole_LB: case e_PlayerRole_RB:
      return PlannedPlayerRole::Defender;
    case e_PlayerRole_CF: return PlannedPlayerRole::Forward;
    default: return PlannedPlayerRole::Midfielder;
  }
}

struct ShapeEntry {
  Vector3 position;
  e_PlayerRole role = e_PlayerRole_GK;
};

// The default policy seeds its own desired shape. These initial conventions
// match the legacy preset, but do not read sim's rule-owned runtime formation.
// Deliberately keep the summation/clamping order for existing policy goldens.
std::vector<ShapeEntry> InitialShape(const model::Team &team) {
  const std::size_t count = !team.formation.empty() ? team.formation.size()
      : !team.tactical_formation.empty() ? team.tactical_formation.size()
      : team.players.size();
  std::vector<ShapeEntry> shape(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (i < team.tactical_formation.size()) {
      const auto &entry = team.tactical_formation[i];
      shape[i].role = entry.role;
      shape[i].position = Vector3(entry.position.x, entry.position.y, 0) * 0.6f +
                          RoleAnchor(entry.role) * 0.4f;
    } else if (team.tactical_formation.empty() && i < team.formation.size()) {
      const auto &entry = team.formation[i];
      shape[i].role = entry.role;
      // Legacy public initial-position lateral convention, not a world pose.
      shape[i].position = Vector3(entry.position.x, entry.position.y * -2.36f, 0) * 0.6f +
                          RoleAnchor(entry.role) * 0.4f;
    }
  }
  constexpr float min_distance = 0.5f;
  for (unsigned iteration = 0; iteration < 10; ++iteration) {
    std::vector<Vector3> offsets(count);
    bool changed = false;
    for (std::size_t first = 0; first + 1 < count; ++first) {
      if (shape[first].role == e_PlayerRole_GK) continue;
      for (std::size_t second = first + 1; second < count; ++second) {
        if (shape[second].role == e_PlayerRole_GK) continue;
        const Vector3 diff = shape[first].position - shape[second].position;
        if (diff.GetLength() < min_distance) {
          changed = true;
          const float factor = 1.0f - diff.GetLength() / min_distance;
          offsets[first] += diff.GetNormalized(Vector3(0, 1, 0)) * min_distance * factor * 0.5f;
          offsets[second] -= diff.GetNormalized(Vector3(0, 1, 0)) * min_distance * factor * 0.5f;
        }
      }
    }
    if (!changed) break;
    for (std::size_t i = 0; i < count; ++i) {
      shape[i].position += offsets[i];
      shape[i].position.coords[0] = std::clamp(shape[i].position.coords[0], -1.f, 1.f);
      shape[i].position.coords[1] = std::clamp(shape[i].position.coords[1], -1.f, 1.f);
    }
  }
  // Explicit initial formation overrides roles, not the declared tactical shape.
  for (std::size_t i = 0; i < team.formation.size(); ++i) shape[i].role = team.formation[i].role;
  return shape;
}
}  // namespace

TacticalBoard MakeTacticalBoard(const model::Team &team, model::TeamSide side,
                               const model::Pitch &pitch) {
  TacticalBoard board;
  board.side = side;
  const auto shape = InitialShape(team);
  const int defend = side == model::TeamSide::Home ? -1 : 1;
  // Team descriptions are validated by Simulation at startup. AI can be
  // constructed before that, and never invents identities for missing profiles.
  for (std::size_t i = 0; i < std::min(shape.size(), team.players.size()); ++i) {
    PlayerDirective directive;
    directive.player = team.players[i].id;
    directive.role = PlannedRole(shape[i].role);
    directive.formation_position = shape[i].position *
        Vector3(-defend * pitch.half_length() * 0.6f,
                -defend * pitch.half_width() * 0.6f, 0);
    board.players.push_back(directive);
  }
  return board;
}
}  // namespace football::ai
