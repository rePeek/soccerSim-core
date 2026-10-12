#ifndef FOOTBALL_SIM_RUNTIME_ROSTER_POSSESSION_HPP
#define FOOTBALL_SIM_RUNTIME_ROSTER_POSSESSION_HPP

#include "football/foundation/time/tick.hpp"

namespace football::ball { class Ball; }
class Player;
class Team;

namespace football::sim {
// Per-roster possession aggregation. This is a team-level algorithm, so it lives
// with Runtime, not with Player: a single Player only owns its own capability
// and state (Player::UpdatePossessionStats). These phases surround ordered
// player execution. All world facts are supplied; Team stores only the resulting
// roster estimates, never a runtime context.
void PrepareTeamPossession(Team& team, const Team& opponent, bool play_authorized,
                           bool set_piece_active, const Player* retainer,
                           const Team* best_possession_team);
void FinishTeamPossession(Team& team, const Team& opponent);
// Refresh actors first, then aggregate this roster. Invoke first/second in order.
void RefreshTeamPossession(Team& team, const Team& opponent, football::ball::Ball& ball,
                           Tick now, const Player* retainer);
void RefreshDesignatedTeamPossessionPlayer(Team& team, const football::ball::Ball& ball);
}  // namespace football::sim

#endif
