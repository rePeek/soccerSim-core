#ifndef FOOTBALL_SIM_RUNTIME_POSSESSION_HPP
#define FOOTBALL_SIM_RUNTIME_POSSESSION_HPP

#include <optional>

#include "football/runtime/match_config.hpp"
#include "runtime/team_state.hpp"

class Player;

namespace football::sim {

// Cross-team possession arbitration: which team (and which player) currently
// owns the ball. Runtime-level, because it compares two teams. The teams are
// supplied in legacy processing order, not necessarily home then away, and each
// is identified by its side plus its runtime-owned match state -- no Team object.
struct PossessionSelection {
  std::optional<TeamSide> best_team;
  Player* designated_player = nullptr;
};

// Read-only arbitration over already-updated reachability/possession facts.
// current_designated must be non-null unless a ball_retainer is supplied.
// Retention is an input physical fact; this function neither owns nor changes it.
PossessionSelection EvaluatePossession(TeamSide first_side,
                                      const TeamMatchState& first_state,
                                      TeamSide second_side,
                                      const TeamMatchState& second_state,
                                      Player* current_designated,
                                      Player* ball_retainer);

}  // namespace football::sim

#endif
