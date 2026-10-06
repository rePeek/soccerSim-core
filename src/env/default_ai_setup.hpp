#ifndef FOOTBALL_ENV_DEFAULT_AI_SETUP_HPP
#define FOOTBALL_ENV_DEFAULT_AI_SETUP_HPP

#include <algorithm>
#include <array>
#include <utility>

#include "ai/default_ai.hpp"
#include "sim/formation.hpp"

namespace football::env {

// Composition-only bootstrap from static declarations, once per AI lifetime.
// Reuse legacy shape arithmetic without giving sim any knowledge of AI boards.
inline ai::DefaultAI MakeDefaultAI(const model::Team &home, const model::Team &away,
                                  const model::Pitch &pitch) {
  std::array<TacticalBoard, 2> boards;
  const std::array<const model::Team *, 2> teams{&home, &away};
  for (unsigned side = 0; side < teams.size(); ++side) {
    auto &board = boards[side];
    board.side = static_cast<model::TeamSide>(side);
    const auto &team = *teams[side];
    const auto formation = BuildFormation(team);
    const int defend = side == 0 ? -1 : 1;
    // Simulation validates roster/formation mismatches at startup, not here.
    for (std::size_t i = 0; i < std::min(formation.size(), team.players.size()); ++i) {
      PlayerDirective directive;
      directive.player = team.players[i].id;
      switch (formation[i].role) {
        case e_PlayerRole_GK: directive.role = PlannedPlayerRole::Goalkeeper; break;
        case e_PlayerRole_CB: case e_PlayerRole_LB: case e_PlayerRole_RB:
          directive.role = PlannedPlayerRole::Defender; break;
        case e_PlayerRole_CF: directive.role = PlannedPlayerRole::Forward; break;
        default: directive.role = PlannedPlayerRole::Midfielder; break;
      }
      directive.formation_position = formation[i].position *
          blunted::Vector3(-defend * pitch.half_length() * 0.6f,
                          -defend * pitch.half_width() * 0.6f, 0);
      board.players.push_back(directive);
    }
  }
  return ai::DefaultAI(std::move(boards));
}

}  // namespace football::env

#endif  // FOOTBALL_ENV_DEFAULT_AI_SETUP_HPP
