#ifndef FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP
#define FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP

#include "sim/player/player_role.hpp"
#include "sim/referee/game_mode.hpp"
#include "football/pitch/pitch.hpp"
#include "sim/simulation_config.hpp"
#include "football/foundation/math/rng.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::ball { class Ball; }

class Player;
class Team;
namespace football::sim::rules { class RuleCommandSink; }

// Rule-owned placement. Referee calls in its established team/RNG order and
// owns the returned taker; AI cannot change restart authority or deadlines.
Player *PositionRestartPlayers(const football::pitch::Pitch& pitch, Team *team,
                              e_GameMode set_piece, Team *other_team,
                              int kickoff_taker_team_id, int taker_team_id,
                              const football::ball::Ball& ball, football::sim::TickSpan regulation,
                              const MatchOptions& options, blunted::Rng& rng,
                              football::sim::rules::RuleCommandSink& commands);

#endif
