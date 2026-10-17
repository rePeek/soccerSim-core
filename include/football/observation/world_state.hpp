#ifndef FOOTBALL_SIM_WORLD_STATE_HPP
#define FOOTBALL_SIM_WORLD_STATE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "football/player/player_definition.hpp"
#include "football/runtime/match_config.hpp"
#include "football/pitch/pitch.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/runtime/phase.hpp"
#include "football/foundation/time/tick.hpp"

struct WorldPlayerState {
  football::player::PlayerId id = football::player::kInvalidPlayerId;
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  football::math::Vector3 position = football::math::Vector3(0);
  football::math::Vector3 velocity = football::math::Vector3(0);
  football::math::Vector3 facing = football::math::Vector3(1, 0, 0);
  bool active = false;
  bool has_possession = false;
  bool lazy = false;
  float max_speed = 0.0f;
  // Rule-assigned positioning during a pending restart, in the same pitch frame.
  std::optional<football::math::Vector3> restart_target;
};

struct WorldTeamState {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  int defending_direction = -1;
  int score = 0;
};

// Value-only observation in one pitch frame (home defends negative x).
// Ball and player positions/directions share this frame in both halves and both
// processing orders. Change of ends never flips the observed defending directions.
// No actor pointers, animation/command queues, mutable runtime or AI objects.
struct WorldState {
  // Absolute simulation timeline. Ordinary dead balls execute every positioning tick.
  std::uint64_t tick = 0;
  MatchPhase phase = MatchPhase::PreMatch;
  // Running half includes ordinary dead balls; half-time/opening ceremonies do not.
  football::sim::TickSpan regulation_time{};
  football::sim::TickSpan ball_in_play_time{};
  bool half_underway = false;
  bool ball_in_play = false;
  football::math::Vector3 ball_position = football::math::Vector3(0);
  std::vector<WorldPlayerState> players;
  football::math::Vector3 ball_velocity = football::math::Vector3(0);
  football::pitch::Pitch pitch;
  std::array<WorldTeamState, 2> teams{{
      {football::sim::TeamSide::Home, -1, 0},
      {football::sim::TeamSide::Away, 1, 0}}};
  // Execution authorization, including a Ready taker before actual restart contact.
  // Use ball_in_play / ball_in_play_time for effective-time measurements.
  bool in_play = false;
  bool in_set_piece = false;
  e_GameMode restart = e_GameMode_Normal;
  std::optional<football::player::PlayerId> restart_taker;
  bool restart_pending = false;
  std::optional<football::player::PlayerId> ball_retainer;
  // Simulation-owned count of ResetSituation discontinuities; resets with a new match.
  std::uint64_t reset_sequence = 0;
};

#endif  // FOOTBALL_SIM_WORLD_STATE_HPP
