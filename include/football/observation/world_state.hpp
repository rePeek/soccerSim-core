#ifndef FOOTBALL_SIM_WORLD_STATE_HPP
#define FOOTBALL_SIM_WORLD_STATE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "football/player/player_definition.hpp"
#include "football/player/player_role.hpp"
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
  // P21.2 read-only summaries for the heuristic policy. They are base values,
  // not mutable match state, and a future learned policy may consume the same
  // fields. No PlayerDefinition, Player* or animation state is copied.
  e_PlayerRole role = e_PlayerRole_GK;
  // 1 = fully rested, 0.01 = exhausted; the runtime fatigue factor as-is.
  float fatigue_inv = 1.0f;
  // The player's own time-to-ball estimate; 1 for the current retainer.
  int time_to_ball_ms = 1000;
  float short_pass_skill = 1.0f;
  float workrate = 1.0f;
  // Rule-assigned positioning during a pending restart, in the same pitch frame.
  std::optional<football::math::Vector3> restart_target;
};

struct WorldTeamState {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  int defending_direction = -1;
  int score = 0;
  // P21.2 team possession facts. `fading_possession_amount` is the runtime's
  // arbitrated possession weight in [0.5, 1.5].
  bool has_possession = false;
  float fading_possession_amount = 1.0f;
  int time_to_ball_ms = 100;
};

// P21.2 ball look-ahead samples, in the same pitch frame as ball_position.
// Index i is the ball centre at kWorldBallPredictionHorizons_ms[i]. The
// runtime Ball model computes them, so the AI never re-implements ball physics.
inline constexpr std::size_t kWorldBallPredictionCount = 4;
inline constexpr int kWorldBallPredictionHorizons_ms[kWorldBallPredictionCount] = {
    200, 350, 500, 650};

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
  std::array<football::math::Vector3, kWorldBallPredictionCount> ball_prediction{};
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
  // Runtime's stable slot bindings; nullopt only for legacy/synthetic inputs.
  // An authoritative empty slot stays empty after send-off, never re-enumerated.
  std::optional<std::array<football::sim::ActiveLineupState, 2>> active_lineups;
};

#endif  // FOOTBALL_SIM_WORLD_STATE_HPP
