#ifndef FOOTBALL_SIM_MATCH_OPTIONS_HPP
#define FOOTBALL_SIM_MATCH_OPTIONS_HPP

// Match-level simulation options only. Teams, pitch, controller assignments and
// runtime objects are passed separately; this is not a composition container.
struct MatchOptions {
  // Scales how fast match time advances; preserves the legacy duration factor.
  float match_duration = 0.027f;
  bool reverse_team_processing = false;
  bool use_magnet = true;
  float left_team_difficulty = 1.0f;
  float right_team_difficulty = 0.6f;
};

#endif  // FOOTBALL_SIM_MATCH_OPTIONS_HPP
