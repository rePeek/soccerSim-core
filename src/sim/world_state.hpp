#ifndef FOOTBALL_SIM_WORLD_STATE_HPP
#define FOOTBALL_SIM_WORLD_STATE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "model/player.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "foundation/math/vector3.hpp"
#include "sim/observation_epoch.hpp"
#include "sim/match_phase.hpp"

struct WorldPlayerState {
  football::model::PlayerId id = football::model::kInvalidPlayerId;
  football::model::TeamSide side = football::model::TeamSide::Home;
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(1, 0, 0);
  bool active = false;
  bool has_possession = false;
  bool lazy = false;
  float max_speed = 0.0f;
};

struct WorldTeamState {
  football::model::TeamSide side = football::model::TeamSide::Home;
  int defending_direction = -1;
  int score = 0;
};

// Value-only observation in one pitch frame (home defends negative x).
// Ball and player positions/directions share this frame in both halves and both
// processing orders. Change of ends never flips the observed defending directions.
// No actor pointers, animation/command queues, mutable runtime or AI objects.
struct WorldState {
  std::uint64_t tick = 0;
  MatchPhase phase = MatchPhase::PreMatch;
  // Scaled football clock, paused during stoppages, not elapsed/observation time.
  std::uint64_t match_time_ms = 0;
  blunted::Vector3 ball_position = blunted::Vector3(0);
  std::vector<WorldPlayerState> players;
  blunted::Vector3 ball_velocity = blunted::Vector3(0);
  football::model::Pitch pitch;
  std::array<WorldTeamState, 2> teams{{
      {football::model::TeamSide::Home, -1, 0},
      {football::model::TeamSide::Away, 1, 0}}};
  bool in_play = false;
  bool in_set_piece = false;
  e_GameMode restart = e_GameMode_Normal;
  std::optional<football::model::PlayerId> restart_taker;
  std::optional<football::model::PlayerId> ball_retainer;
  // Match-owned count of ResetSituation discontinuities; resets with a new match.
  std::uint64_t reset_sequence = 0;
  // Fresh per Match, including reset/re-init and independently constructed engines.
  ObservationEpoch simulation_epoch;
};

#endif  // FOOTBALL_SIM_WORLD_STATE_HPP
