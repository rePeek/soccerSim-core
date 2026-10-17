#ifndef FOOTBALL_SIM_RULES_REFEREE_TICK_FACTS_HPP
#define FOOTBALL_SIM_RULES_REFEREE_TICK_FACTS_HPP

#include "football/pitch/pitch.hpp"
#include "football/runtime/phase.hpp"
#include "runtime/team_view.hpp"
#include "observation/pitch_frame.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::ball { class Ball; }

namespace football::sim::rules {

// Borrowed for one referee call only. football::ball::Ball/actors remain live across synchronous
// reset commands; copying their position before a reset would change readiness.
// Configuration and RNG are separate explicit Process arguments.
// Teams are borrowed views (identity + direction + roster slice + runtime match
// state); Rules never holds or receives a Team object.
struct RefereeTickFacts {
  Tick now;
  MatchPhase phase;
  bool play_authorized;
  bool set_piece_active;
  bool goal_scored;
  const football::ball::Ball& ball;
  const football::pitch::Pitch& pitch;
  TickSpan regulation;
  football::sim::TeamView home;
  football::sim::TeamView away;
  int first_team;
  // Legacy team slots (0/1), -1 when the fact is unknown.
  int last_touch_team = -1;
  int last_goal_team = -1;
  PitchFrameTransform ball_to_home;
  PitchFrameTransform stadium_to_home;
};

} // namespace football::sim::rules
#endif
