#ifndef FOOTBALL_SIM_RUNTIME_TICK_FACTS_HPP
#define FOOTBALL_SIM_RUNTIME_TICK_FACTS_HPP

#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"
#include "football/pitch/pitch_frame.hpp"
#include "football/runtime/phase.hpp"
#include "runtime/team_view.hpp"

namespace football::ball { class Ball; }

namespace football::sim {

// Borrowed Runtime fact bundle for one execution instant, consumed by the
// restart controller (placement needs live team rosters and the Ball). It is
// never handed to the Referee: Runtime derives the Referee's own value facts
// (rules::RefereeTickFacts) from it.
struct RuntimeTickFacts {
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
  // Sampling conversions for one execution instant. Both are 180-degree
  // rotations and therefore involutions:
  // - ball_to_home maps the between-tick processing frame to the published
  //   Home Pitch Frame (the first processing team's dynamic side);
  // - stadium_to_home maps the Home Pitch Frame <-> the Stadium Frame, derived
  //   from the home team's defending end, never from MatchPhase.
  PitchFrameTransform ball_to_home;
  PitchFrameTransform stadium_to_home;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TICK_FACTS_HPP
