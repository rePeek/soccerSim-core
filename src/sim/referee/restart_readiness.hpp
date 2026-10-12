#ifndef FOOTBALL_SIM_RULES_RESTART_READINESS_HPP
#define FOOTBALL_SIM_RULES_RESTART_READINESS_HPP

#include <span>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "sim/player/player_role.hpp"
#include "sim/referee/game_mode.hpp"
#include "football/pitch/pitch.hpp"

class Player;
class Team;

// Targets are in the fixed home-pitch frame, independent of processing mirrors.
// Planning does not move actors, select animations, or consume RNG.
struct RestartPlayerTarget {
  Player* player = nullptr;
  blunted::Vector3 position;
};
struct RestartPlan {
  e_GameMode mode = e_GameMode_Normal;
  Team* team = nullptr;
  Player* taker = nullptr;
  blunted::Vector3 ball_position;
  std::vector<RestartPlayerTarget> players;
};

// Explicit home-frame ball position and home-then-away active roster order.
RestartPlan PlanRestart(const football::pitch::Pitch& pitch,
                        const blunted::Vector3& home_ball_position,
                        std::span<Player* const> active_players,
                        e_GameMode mode, Team& team);
bool RestartPlayersReady(const RestartPlan& plan, const football::pitch::Pitch& pitch);
// Deterministic rule fallback for actors that fail to get ready by the timeout.
// This repairs positions, never invents a ball contact or an AI decision.
void PlaceRestartPlayersAtTimeout(const RestartPlan& plan);

#endif
