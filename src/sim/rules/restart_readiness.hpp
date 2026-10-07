#ifndef FOOTBALL_SIM_RULES_RESTART_READINESS_HPP
#define FOOTBALL_SIM_RULES_RESTART_READINESS_HPP

#include <vector>

#include "foundation/math/vector3.hpp"
#include "model/football_types.hpp"

class Match;
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

// Planning requires the referee mirror scope, with the ball already placed.
RestartPlan PlanRestart(Match& match, e_GameMode mode, Team& team);
bool RestartPlayersReady(const RestartPlan& plan);
// Deterministic rule fallback for actors that fail to get ready by the timeout.
// This repairs positions, never invents a ball contact or an AI decision.
void PlaceRestartPlayersAtTimeout(const RestartPlan& plan);

#endif
