#ifndef FOOTBALL_SIM_RUNTIME_TEAM_FACTS_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_FACTS_HPP

#include <span>

#include "player/player.hpp"
#include "runtime/team_state.hpp"

namespace football::sim {

// Per-player cached projection of Team's live values. Simulation controls when
// publication occurs; changing storage ownership must never delay or batch it.
inline PlayerTeamFacts TeamFactsOf(const TeamOrientation& orientation,
                                   const TeamMatchState& state) {
  return PlayerTeamFacts{orientation.defending_direction,
                         orientation.execution_direction,
                         orientation.on_original_side(),
                         state.possession_amount,
                         state.fading_possession_amount,
                         state.designated_player};
}

// Publishes the current facts to every player of the slice, in declaration order.
inline void PublishTeamFacts(std::span<Player* const> players,
                             const TeamOrientation& orientation,
                             const TeamMatchState& state) {
  const PlayerTeamFacts facts = TeamFactsOf(orientation, state);
  for (Player* player : players) player->SetTeamContext(facts);
}

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_FACTS_HPP
