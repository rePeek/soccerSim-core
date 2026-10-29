#ifndef FOOTBALL_SIM_RUNTIME_RESTART_PLANNER_HPP
#define FOOTBALL_SIM_RUNTIME_RESTART_PLANNER_HPP

#include <span>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "football/runtime/active_lineup.hpp"
#include "football/team/formation_preset.hpp"
#include "football/player/formation_entry.hpp"
#include "football/team/set_piece_orders.hpp"
#include "runtime/restart_context.hpp"

class Player;

namespace football::sim {

// Borrowed static Team restart configuration plus the live end it defends for
// one side. The direction is supplied by Runtime from the team's
// TeamOrientation; the planner must never infer it from the team slot, because
// the ends swap at half time. All pointers may be null: the planner then keeps
// the legacy (nearest / formation) behaviour.
struct RestartTeamPlanning {
  int defending_direction = -1;
  const football::team::SetPieceOrders* orders = nullptr;
  const football::team::FormationPresets* presets = nullptr;
  const football::sim::ActiveLineupState* lineup = nullptr;
  // Full ordered roster and its Runtime-owned formation, aligned by index.
  std::span<Player* const> players;
  std::span<const FormationEntry> formation;
};

// Formation entry for one actor, resolved through the planning table's
// ordered roster. nullptr when the actor is absent (bench/sent-off).
inline const FormationEntry* FormationEntryFor(
    const RestartTeamPlanning& planning, const Player* player) {
  for (std::size_t i = 0; i < planning.players.size(); ++i) {
    if (planning.players[i] == player) return &planning.formation[i];
  }
  return nullptr;
}

// Both sides: the awarded team uses Attacking presets, its opponent Defending.
struct RestartPlanningTable {
  RestartTeamPlanning home;
  RestartTeamPlanning away;

  const RestartTeamPlanning& For(int team_id) const {
    return team_id == 0 ? home : away;
  }
};

// Explicit Stadium Frame ball position and home-then-away active roster order.
// Runtime supplies live eligibility (active actors) and the current defending
// directions; the planner selects the taker and computes candidates. It only
// produces data: legality is enforced here, but Runtime owns every actor/ball
// mutation.
RestartPlan PlanRestart(const football::pitch::Pitch& pitch,
                        const football::math::Vector3& ball_position,
                        std::span<Player* const> active_players,
                        e_GameMode mode, int team_id,
                        const RestartPlanningTable& planning);
// Runtime-supplied actor index for one execution instant. Plans store stable
// ids; the index resolves them back to live actors, including a dismissed
// player that is still present in the plan, so the caller can skip or replan.
struct RestartRoster {
  std::span<Player* const> home;
  std::span<Player* const> away;

  Player* Find(football::player::PlayerId id) const;
};

bool RestartPlayersReady(const RestartPlan& plan, const football::pitch::Pitch& pitch,
                         const RestartRoster& roster,
                         const RestartPlanningTable& planning);
// Deterministic runtime fallback for actors that fail to get ready by the
// timeout. This repairs positions, never invents a ball contact or an AI
// decision.
void PlaceRestartPlayersAtTimeout(const RestartPlan& plan, const RestartRoster& roster);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_RESTART_PLANNER_HPP
