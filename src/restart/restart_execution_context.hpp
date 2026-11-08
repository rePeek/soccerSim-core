#ifndef FOOTBALL_SIM_RESTART_EXECUTION_CONTEXT_HPP
#define FOOTBALL_SIM_RESTART_EXECUTION_CONTEXT_HPP

#include "football/foundation/time/tick.hpp"
#include "football/match/phase.hpp"
#include "football/pitch/pitch.hpp"
#include "team/team_view.hpp"

namespace football::ball { class Ball; }

namespace football::sim {

// Borrowed execution input for one restart instant. It contains only domain
// values and team-owned views: the restart controller never receives Runtime
// private types, a Simulation reference or a mutable match aggregate.
struct RestartExecutionContext {
  Tick now;
  MatchPhase phase = MatchPhase::PreMatch;
  bool play_authorized = false;
  bool set_piece_active = false;
  const football::ball::Ball& ball;
  const football::pitch::Pitch& pitch;
  TickSpan regulation{};
  // Borrowed team views: identity, defending direction, roster and formation.
  football::sim::TeamView home;
  football::sim::TeamView away;
  int first_team = 0;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RESTART_EXECUTION_CONTEXT_HPP
