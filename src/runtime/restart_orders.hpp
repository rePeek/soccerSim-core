#ifndef FOOTBALL_SIM_RUNTIME_RESTART_ORDERS_HPP
#define FOOTBALL_SIM_RUNTIME_RESTART_ORDERS_HPP

#include <span>

#include "football/player/player_definition.hpp"
#include "football/referee/game_mode.hpp"
#include "football/team/set_piece_orders.hpp"

namespace football::sim {

// Declared taker priority for one restart, already resolved to the lists the
// planner should try in order. Empty spans mean "this team declared nobody" and
// preserve the legacy nearest-active-player selection exactly.
struct RestartPriority {
  std::span<const football::player::PlayerId> primary;
  std::span<const football::player::PlayerId> secondary;
};

// Maps a rules-owned restart mode to the declaring team's static order. Runtime
// owns the mapping because `e_GameMode` is a rules-domain value; Team only owns
// the declared lists.
//
// `home_frame_ball_y` is the fixed home-pitch ball y. `attacking_side` is the
// taking team's attack direction (+1 towards +X). The corner side is
// team-relative: a team's left is the corner y on its attack side. `FreeKick`
// tries the direct list first, then the indirect list, because this engine's
// single FreeKick mode does not yet distinguish the two rules.
inline RestartPriority RestartPriorityFor(
    const football::team::SetPieceOrders& orders, e_GameMode mode,
    float home_frame_ball_y, int attacking_side) noexcept {
  switch (mode) {
    case e_GameMode_Penalty:
      return {orders.penalties, {}};
    case e_GameMode_FreeKick:
      return {orders.direct_free_kicks, orders.indirect_free_kicks};
    case e_GameMode_Corner:
      return home_frame_ball_y * static_cast<float>(attacking_side) > 0.0f
          ? RestartPriority{orders.corners_left, {}}
          : RestartPriority{orders.corners_right, {}};
    case e_GameMode_ThrowIn:
      return {orders.throw_ins, {}};
    default:
      // Kick-off and goal kick have no declared taker category.
      return {};
  }
}

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_RESTART_ORDERS_HPP
