#ifndef FOOTBALL_SIM_RUNTIME_TEAM_POSSESSION_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_POSSESSION_HPP

#include <optional>
#include <span>

#include "football/runtime/match_config.hpp"
#include "runtime/team_state.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::ball { class Ball; }
class Player;

namespace football::sim {
// Per-team possession aggregation. Team-level algorithms owned by Runtime:
// they take the caller's borrowed Player slice (Team::Players()), the live
// TeamOrientation and the live TeamMatchState directly. Ownership stays with Team, so
// these helpers never take or expose a player container. A single Player only
// owns its own capability/state (Player::UpdatePossessionStats). These phases
// surround ordered player execution; all world facts are supplied. Publication
// happens exactly where the legacy Team setters published, so player-cached
// facts never lag a tick.
void PrepareTeamPossession(TeamSide side, std::span<Player* const> players,
                           const TeamOrientation& frame, TeamMatchState& state,
                           const TeamMatchState& opponent_state,
                           bool play_authorized, bool set_piece_active,
                           const Player* retainer,
                           std::optional<TeamSide> best_possession_team);
void FinishTeamPossession(std::span<Player* const> players,
                          const TeamOrientation& frame, TeamMatchState& state,
                          const TeamMatchState& opponent_state);
// Refresh actors first, then aggregate this team. Invoke first/second in order.
void RefreshTeamPossession(std::span<Player* const> players,
                           const TeamOrientation& frame, TeamMatchState& state,
                           const TeamMatchState& opponent_state,
                           football::ball::Ball& ball, Tick now,
                           const Player* retainer);
void RefreshDesignatedTeamPossessionPlayer(std::span<Player* const> players,
                                           const TeamOrientation& frame,
                                           TeamMatchState& state,
                                           const football::ball::Ball& ball);
// Lowest time-to-ball active player, the legacy Team::GetBestPossessionPlayer.
Player* BestPossessionPlayer(std::span<Player* const> players);
}  // namespace football::sim

#endif
