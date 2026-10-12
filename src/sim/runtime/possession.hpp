#ifndef FOOTBALL_SIM_RUNTIME_POSSESSION_HPP
#define FOOTBALL_SIM_RUNTIME_POSSESSION_HPP

class Team;
class Player;

namespace football::sim {

// Cross-team possession arbitration: which team (and which player) currently
// owns the ball. Runtime-level, because it compares two rosters.
struct PossessionSelection {
  Team* best_team = nullptr;
  Player* designated_player = nullptr;
};

// Read-only arbitration over already-updated reachability/possession facts.
// Teams are supplied in legacy processing order, not necessarily home then away.
// current_designated must be non-null unless a ball_retainer is supplied.
// Retention is an input physical fact; this function neither owns nor changes it.
PossessionSelection EvaluatePossession(Team& first, Team& second,
                                      Player* current_designated,
                                      Player* ball_retainer);

}  // namespace football::sim

#endif
