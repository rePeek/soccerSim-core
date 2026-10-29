#ifndef FOOTBALL_SIM_RUNTIME_TEAM_POSSESSION_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_POSSESSION_HPP

#include <cstdint>
#include <optional>
#include <span>

#include "football/foundation/time/tick.hpp"
#include "football/player/execution/player_ball_relation.hpp"
#include "football/runtime/match_config.hpp"
#include "runtime/team_state.hpp"

namespace football::ball { class Ball; }
class Player;

namespace football::sim {
// Per-team possession aggregation. Team-level algorithms owned by Runtime: they
// take the caller's borrowed Player slice (Team::Players()) and the Runtime-owned
// per-actor ball relations, both aligned by index. Ownership of the relation
// stays with Runtime, so a Player only reports its own capability and state.
//
// Prepare and Finish are pure policy over team facts. Refresh is the single
// writer of the per-actor relations and of the aggregated team estimate; it runs
// between ordered player execution, exactly where the legacy per-player update
// ran, so refresh frequency and results are unchanged.
void PrepareTeamPossession(TeamSide side, std::span<Player* const> players,
                           const TeamOrientation& frame, TeamMatchState& state,
                           const TeamMatchState& opponent_state,
                           bool play_authorized, bool set_piece_active,
                           const Player* retainer,
                           std::optional<TeamSide> best_possession_team);
void FinishTeamPossession(
    std::span<Player* const> players,
    std::span<const football::player::PlayerBallRelation> relations,
    const TeamOrientation& frame, TeamMatchState& state,
    const TeamMatchState& opponent_state);
// Refresh the per-actor ball relations first, then aggregate this team. The
// relations and their staggering phases are Runtime-owned and aligned with
// `players`. Invoke the two teams in order.
void RefreshTeamPossession(
    std::span<Player* const> players,
    std::span<football::player::PlayerBallRelation> relations,
    std::span<const std::uint8_t> phases, const TeamOrientation& frame,
    TeamMatchState& state, const TeamMatchState& opponent_state,
    football::ball::Ball& ball, Tick now, const Player* retainer);
void RefreshDesignatedTeamPossessionPlayer(std::span<Player* const> players,
                                           const TeamOrientation& frame,
                                           TeamMatchState& state,
                                           const football::ball::Ball& ball);
// Lowest time-to-ball active player, the legacy Team::GetBestPossessionPlayer.
Player* BestPossessionPlayer(
    std::span<Player* const> players,
    std::span<const football::player::PlayerBallRelation> relations);
}  // namespace football::sim

#endif
