#ifndef FOOTBALL_SIM_RUNTIME_TEAM_FACTS_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_FACTS_HPP

#include <span>

#include "player/player.hpp"
#include "runtime/roster.hpp"
#include "runtime/team_state.hpp"

namespace football::sim {

// The per-tick projection of a team's runtime-owned state that every player
// caches. Runtime owns both the state and this projection; Team only forwards to
// it while it still exists (P6b-1b).
inline PlayerTeamFacts TeamFactsOf(const TeamFrame& frame,
                                   const TeamMatchState& state) {
  return PlayerTeamFacts{frame.dynamic_side, frame.static_side,
                         frame.dynamic_side == frame.static_side,
                         state.possession_amount,
                         state.fading_possession_amount,
                         state.designated_player};
}

// Publishes the current facts to every player of the slice, in roster order.
inline void PublishTeamFacts(std::span<Player* const> players,
                             const TeamFrame& frame,
                             const TeamMatchState& state) {
  const PlayerTeamFacts facts = TeamFactsOf(frame, state);
  for (Player* player : players) player->SetTeamContext(facts);
}

// Same, for a caller that holds the owning Roster.
inline void PublishTeamFacts(const Roster& roster, const TeamFrame& frame,
                             const TeamMatchState& state) {
  PublishTeamFacts(roster.Players(), frame, state);
}

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_FACTS_HPP
