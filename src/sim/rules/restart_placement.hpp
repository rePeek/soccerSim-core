#ifndef FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP
#define FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP

#include "model/football_types.hpp"
#include "sim/simulation_config.hpp"
#include "foundation/math/rng.hpp"
#include "sim/time/tick.hpp"

namespace football::ball { class Ball; }

class Player;
class Team;
namespace football::sim::rules { class RuleCommandSink; }

// Rule-owned placement. Referee calls in its established team/RNG order and
// owns the returned taker; AI cannot change restart authority or deadlines.
Player *PositionRestartPlayers(Team *team, e_GameMode set_piece, Team *other_team,
                              int kickoff_taker_team_id, int taker_team_id,
                              const football::ball::Ball& ball, football::sim::TickSpan regulation,
                              const MatchOptions& options, blunted::Rng& rng,
                              football::sim::rules::RuleCommandSink& commands);

#endif
