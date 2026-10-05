#ifndef FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP
#define FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP

#include "model/football_types.hpp"

class Player;
class Team;

// Rule-owned placement. Referee calls in its established team/RNG order and
// owns the returned taker; AI cannot change restart authority or deadlines.
Player *PositionRestartPlayers(Team *team, e_GameMode set_piece, Team *other_team,
                              int kickoff_taker_team_id, int taker_team_id);

#endif
