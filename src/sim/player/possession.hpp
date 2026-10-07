#ifndef FOOTBALL_SIM_PLAYER_POSSESSION_HPP
#define FOOTBALL_SIM_PLAYER_POSSESSION_HPP

#include "sim/time/tick.hpp"

class Ball;
class Player;
class Team;

namespace football::sim::player {
// These phases surround ordered player execution. All world facts are supplied;
// Team stores only the resulting roster estimates, never a runtime context.
void PrepareTeamPossession(Team& team, const Team& opponent, bool play_authorized,
                          bool set_piece_active, const Player* retainer,
                          const Team* best_possession_team);
void FinishTeamPossession(Team& team, const Team& opponent);
// Refresh actors first, then aggregate this roster. Invoke first/second in order.
void RefreshTeamPossession(Team& team, const Team& opponent, Ball& ball,
                          Tick now, const Player* retainer);
void RefreshDesignatedTeamPossessionPlayer(Team& team, const Ball& ball);
} // namespace football::sim::player

#endif
