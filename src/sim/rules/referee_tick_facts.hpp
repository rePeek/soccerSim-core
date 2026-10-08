#ifndef FOOTBALL_SIM_RULES_REFEREE_TICK_FACTS_HPP
#define FOOTBALL_SIM_RULES_REFEREE_TICK_FACTS_HPP

#include "model/pitch.hpp"
#include "sim/rules/phase.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "foundation/time/tick.hpp"

namespace football::ball { class Ball; }
class Team;

namespace football::sim::rules {

// Borrowed for one referee call only. football::ball::Ball/actors remain live across synchronous
// reset commands; copying their position before a reset would change readiness.
// Configuration and RNG are separate explicit Process arguments.
struct RefereeTickFacts {
  Tick now;
  MatchPhase phase;
  bool play_authorized;
  bool set_piece_active;
  bool goal_scored;
  const football::ball::Ball& ball;
  const football::model::Pitch& pitch;
  TickSpan regulation;
  Team& home;
  Team& away;
  int first_team;
  Team* last_touch_team;
  Team* last_goal_team;
  PitchFrameTransform ball_to_home;
  PitchFrameTransform stadium_to_home;
};

} // namespace football::sim::rules
#endif
