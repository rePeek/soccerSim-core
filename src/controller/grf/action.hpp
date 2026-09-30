#ifndef FOOTBALL_CONTROLLER_GRF_ACTION_HPP
#define FOOTBALL_CONTROLLER_GRF_ACTION_HPP

// Stable GRF external-action protocol. This is a controller input protocol,
// not an environment or simulation concept.
enum Action {
  game_idle = 0,
  game_left = 1,
  game_top_left = 2,
  game_top = 3,
  game_top_right = 4,
  game_right = 5,
  game_bottom_right = 6,
  game_bottom = 7,
  game_bottom_left = 8,
  game_long_pass = 9,
  game_high_pass = 10,
  game_short_pass = 11,
  game_shot = 12,
  game_keeper_rush = 13,
  game_sliding = 14,
  game_pressure = 15,
  game_team_pressure = 16,
  game_switch = 17,
  game_sprint = 18,
  game_dribble = 19,
  game_release_direction = 20,
  game_release_long_pass = 21,
  game_release_high_pass = 22,
  game_release_short_pass = 23,
  game_release_shot = 24,
  game_release_keeper_rush = 25,
  game_release_sliding = 26,
  game_release_pressure = 27,
  game_release_team_pressure = 28,
  game_release_switch = 29,
  game_release_sprint = 30,
  game_release_dribble = 31,
  game_builtin_ai = 32
};

#endif  // FOOTBALL_CONTROLLER_GRF_ACTION_HPP
