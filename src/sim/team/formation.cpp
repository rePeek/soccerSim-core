// Role adaptation/spacing arithmetic retained from the former TeamData.
#include "sim/team/formation.hpp"

namespace {

Vector3 DefaultRolePosition(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK: return Vector3(-1.0, 0.0, 0);
    case e_PlayerRole_CB: return Vector3(-1.0, 0.0, 0);
    case e_PlayerRole_LB: return Vector3(-0.8, 0.8, 0);
    case e_PlayerRole_RB: return Vector3(-0.8, -0.8, 0);
    case e_PlayerRole_DM: return Vector3(-0.5, 0.0, 0);
    case e_PlayerRole_CM: return Vector3(0.0, 0.0, 0);
    case e_PlayerRole_LM: return Vector3(0.0, 1.0, 0);
    case e_PlayerRole_RM: return Vector3(0.0, -1.0, 0);
    case e_PlayerRole_AM: return Vector3(0.5, 0.0, 0);
    case e_PlayerRole_CF: return Vector3(1.0, 0.0, 0);
    default: return Vector3(0.0, 0.0, 0);
  }
}

}  // namespace

std::vector<FormationEntry> BuildFormation(const football::model::Team& team) {
  const std::size_t count = !team.formation.empty() ? team.formation.size()
      : !team.tactical_formation.empty() ? team.tactical_formation.size()
      : team.players.size();
  std::vector<FormationEntry> formation(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (i < team.tactical_formation.size()) {
      const auto& entry = team.tactical_formation[i];
      formation[i].role = entry.role;
      formation[i].position = Vector3(entry.position.x, entry.position.y, 0) * 0.6f +
                             DefaultRolePosition(entry.role) * 0.4f;
    } else if (team.tactical_formation.empty() && i < team.formation.size()) {
      const auto& entry = team.formation[i];
      formation[i].role = entry.role;
      formation[i].position = Vector3(entry.position.x,
                                      entry.position.y * FORMATION_Y_SCALE, 0) * 0.6f +
                             DefaultRolePosition(entry.role) * 0.4f;
    }
  }

  // Keep the legacy iteration, summation and clamping order exactly.
  float minDistanceFraction = 0.5f;
  unsigned int maxIterations = 10;
  unsigned int iterations = 0;
  bool changed = true;
  while (changed && iterations < maxIterations) {
    std::vector<Vector3> offset(count);
    changed = false;
    for (std::size_t p1 = 0; p1 + 1 < count; ++p1) {
      if (formation[p1].role == e_PlayerRole_GK) continue;
      for (std::size_t p2 = p1 + 1; p2 < count; ++p2) {
        if (formation[p2].role == e_PlayerRole_GK) continue;
        Vector3 diff = formation[p1].position - formation[p2].position;
        if (diff.GetLength() < minDistanceFraction) {
          changed = true;
          float distanceFactor = 1.0f - (diff.GetLength() / minDistanceFraction);
          offset[p1] += diff.GetNormalized(Vector3(0, 1, 0)) *
                        minDistanceFraction * distanceFactor * 0.5f;
          offset[p2] -= diff.GetNormalized(Vector3(0, 1, 0)) *
                        minDistanceFraction * distanceFactor * 0.5f;
        }
      }
    }
    if (changed) {
      for (std::size_t p = 0; p < count; ++p) {
        formation[p].position += offset[p];
        formation[p].position.coords[0] = clamp(formation[p].position.coords[0], -1, 1);
        formation[p].position.coords[1] = clamp(formation[p].position.coords[1], -1, 1);
      }
    }
    ++iterations;
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (team.formation.empty()) {
      formation[i].start_position = formation[i].position;
    } else {
      const auto& entry = team.formation[i];
      formation[i].start_position = Vector3(entry.position.x,
                                           entry.position.y * FORMATION_Y_SCALE, 0);
      formation[i].lazy = entry.lazy;
      formation[i].role = entry.role;
      formation[i].controllable = entry.controllable;
    }
  }
  return formation;
}
