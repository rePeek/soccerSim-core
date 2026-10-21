#ifndef FOOTBALL_SIM_RULES_REFEREE_TICK_FACTS_HPP
#define FOOTBALL_SIM_RULES_REFEREE_TICK_FACTS_HPP

#include "football/pitch/pitch.hpp"
#include "football/runtime/phase.hpp"
#include "football/foundation/time/tick.hpp"
#include "referee/referee_facts.hpp"


namespace football::sim::rules {

// Read-only rule facts for one referee call only. Runtime samples them from the
// live ball, teams and pitch; the Referee holds no Ball, TeamView, Player or
// Runtime object and never mutates anything.
struct RefereeTickFacts {
  Tick now;
  MatchPhase phase;
  bool play_authorized;
  bool set_piece_active;
  bool goal_scored;
  RefereeBallFact ball;
  const football::pitch::Pitch& pitch;
  TickSpan regulation;
  RefereeTeamFact home;
  RefereeTeamFact away;
  int first_team;
  // Legacy team slots (0/1), -1 when the fact is unknown.
  int last_touch_team = -1;
  int last_goal_team = -1;
};

} // namespace football::sim::rules
#endif
