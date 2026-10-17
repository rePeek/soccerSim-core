#ifndef FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP
#define FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP

#include "football/observation/world_state.hpp"
#include "football/pitch/pitch.hpp"
#include "runtime/team_view.hpp"

namespace football::ball { class Ball; }
class Player;
class Referee;
namespace football::pitch { class Pitch; }

// Observation-only view assembled by the composition root. It serves exactly one
// BuildWorldState call, is never retained by actors, and is not a runtime context.
struct WorldStateSource {
  football::sim::Tick now;
  MatchPhase phase;
  football::sim::TickSpan regulation_time;
  football::sim::TickSpan ball_in_play_time;
  const bool& half_underway;
  const bool& ball_in_play;
  std::uint64_t reset_sequence;
  const football::ball::Ball& ball;
  const football::pitch::Pitch& pitch;
  bool in_play;
  bool in_set_piece;
  Referee& referee;
  const Player* ball_retainer;
  int score_home;
  int score_away;
  // Borrowed team views; observation reads identity, direction and roster only.
  football::sim::TeamView home;
  football::sim::TeamView away;
  int first_team;  // 0 or 1; whose runtime frame the between-tick ball shares
};

// Copies the simulation's current authoritative state into a passive value
// snapshot for decision systems, replay, training, and diagnostics.
WorldState BuildWorldState(const WorldStateSource& source);

#endif  // FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP
