#ifndef FOOTBALL_SIM_RUNTIME_RESTART_PLANNER_HPP
#define FOOTBALL_SIM_RUNTIME_RESTART_PLANNER_HPP

#include <span>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "runtime/restart_context.hpp"

class Player;

namespace football::sim {

// Explicit home-frame ball position and home-then-away active roster order.
// The planner only computes data; Runtime owns every actor/ball mutation.
RestartPlan PlanRestart(const football::pitch::Pitch& pitch,
                        const football::math::Vector3& home_ball_position,
                        std::span<Player* const> active_players,
                        e_GameMode mode, int team_id);
bool RestartPlayersReady(const RestartPlan& plan, const football::pitch::Pitch& pitch);
// Deterministic runtime fallback for actors that fail to get ready by the
// timeout. This repairs positions, never invents a ball contact or an AI
// decision.
void PlaceRestartPlayersAtTimeout(const RestartPlan& plan);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_RESTART_PLANNER_HPP
