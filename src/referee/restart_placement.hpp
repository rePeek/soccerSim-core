#ifndef FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP
#define FOOTBALL_SIM_RULES_RESTART_PLACEMENT_HPP

#include "football/player/player_role.hpp"
#include "football/referee/game_mode.hpp"
#include "football/pitch/pitch.hpp"
#include "football/runtime/simulation_config.hpp"
#include "football/foundation/math/rng.hpp"
#include "football/foundation/time/tick.hpp"
#include "sim/runtime/team_view.hpp"
namespace football::ball { class Ball; }

class Player;
namespace football::sim::rules { class RuleCommandSink; }

// Rule-owned placement. Referee calls in its established team/RNG order and
// owns the returned taker; AI cannot change restart authority or deadlines.
Player *PositionRestartPlayers(const football::pitch::Pitch& pitch,
                              const football::sim::TeamView& team, e_GameMode set_piece,
                              const football::sim::TeamView& other_team,
                              int kickoff_taker_team_id, int taker_team_id,
                              const football::ball::Ball& ball, football::sim::TickSpan regulation,
                              const MatchOptions& options, football::math::Rng& rng,
                              football::sim::rules::RuleCommandSink& commands);

#endif
