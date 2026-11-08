#ifndef FOOTBALL_SIM_RUNTIME_RESTART_PLACEMENT_HPP
#define FOOTBALL_SIM_RUNTIME_RESTART_PLACEMENT_HPP

#include "football/pitch/pitch.hpp"
#include "football/runtime/simulation_config.hpp"
#include "football/foundation/time/tick.hpp"
#include "runtime/team_view.hpp"
namespace football::ball { class Ball; }

class Player;

// Opening/half-time kickoff placement only. Ordinary restarts use PlanRestart.
// Called in processing-team order; the awarded team's nearest active player
// is returned as taker. Preserves the ceremony's ordered actor resets.
Player* PositionKickOffPlayers(const football::pitch::Pitch& pitch,
                              const football::sim::TeamView& team,
                              const football::sim::TeamView& other_team,
                              int kickoff_taker_team_id, int taker_team_id,
                              const football::ball::Ball& ball,
                              football::sim::TickSpan regulation,
                              const MatchOptions& options);

#endif
