#ifndef FOOTBALL_SIM_MATCH_OPTIONS_HPP
#define FOOTBALL_SIM_MATCH_OPTIONS_HPP

#include "foundation/math/vector3.hpp"

// Match-level simulation options only. Teams, pitch and runtime objects are
// passed separately; this is not a composition container. Every value is
// snapshotted at initialization and never read from ambient state afterwards.
struct MatchOptions {
  // Scales how fast match time advances; preserves the legacy duration factor.
  float match_duration = 0.027f;
  bool reverse_team_processing = false;
  bool use_magnet = true;
  float left_team_difficulty = 1.0f;
  float right_team_difficulty = 0.6f;
  unsigned int game_engine_random_seed = 42;
  int second_half = 999999999;
  bool offsides = true;
  // Kickoff and second-half ball position in runtime units. The legacy episode
  // conversion multiplied public (0, 0, 0) by (54.4, -83.6, 1), which leaves
  // the historical signed zero on y; keeping it preserves bit-identical replays.
  blunted::Vector3 ball_position = blunted::Vector3(0.0f, -0.0f, 0.0f);
  // Derived once from the effective initial formations at initialization.
  bool left_team_owns_ball = false;
  bool dynamic_player_selection = true;
};

#endif  // FOOTBALL_SIM_MATCH_OPTIONS_HPP
