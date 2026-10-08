#ifndef FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP
#define FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP

#include "sim/observation/world_state.hpp"

namespace football::ball { class Ball; }
class Player;
class Team;
class Referee;
namespace football::model { class Pitch; }

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
  const football::model::Pitch& pitch;
  bool in_play;
  bool in_set_piece;
  Referee& referee;
  const Player* ball_retainer;
  int score_home;
  int score_away;
  Team& home;
  Team& away;
  int first_team;  // 0 or 1; whose runtime frame the between-tick ball shares
};

// Copies the simulation's current authoritative state into a passive value
// snapshot for decision systems, replay, training, and diagnostics.
WorldState BuildWorldState(const WorldStateSource& source);

#endif  // FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP