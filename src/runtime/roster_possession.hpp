#ifndef FOOTBALL_SIM_RUNTIME_ROSTER_POSSESSION_HPP
#define FOOTBALL_SIM_RUNTIME_ROSTER_POSSESSION_HPP

#include <optional>

#include "football/runtime/match_config.hpp"
#include "runtime/roster.hpp"
#include "runtime/team_state.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::ball { class Ball; }
class Player;

namespace football::sim {
// Per-roster possession aggregation. Team-level algorithms owned by Runtime:
// they take the roster, the borrowed TeamFrame and the live TeamMatchState
// directly, so no Team object is involved. A single Player only owns its own
// capability/state (Player::UpdatePossessionStats). These phases surround
// ordered player execution; all world facts are supplied. Publication happens
// exactly where the legacy Team setters published, so player-cached facts never
// lag a tick.
void PrepareTeamPossession(TeamSide side, const Roster& roster,
                           const TeamFrame& frame, TeamMatchState& state,
                           const TeamMatchState& opponent_state,
                           bool play_authorized, bool set_piece_active,
                           const Player* retainer,
                           std::optional<TeamSide> best_possession_team);
void FinishTeamPossession(const Roster& roster, const TeamFrame& frame,
                          TeamMatchState& state,
                          const TeamMatchState& opponent_state);
// Refresh actors first, then aggregate this roster. Invoke first/second in order.
void RefreshTeamPossession(const Roster& roster, const TeamFrame& frame,
                           TeamMatchState& state,
                           const TeamMatchState& opponent_state,
                           football::ball::Ball& ball, Tick now,
                           const Player* retainer);
void RefreshDesignatedTeamPossessionPlayer(const Roster& roster,
                                           const TeamFrame& frame,
                                           TeamMatchState& state,
                                           const football::ball::Ball& ball);
// Lowest time-to-ball active player, the legacy Team::GetBestPossessionPlayer.
Player* BestPossessionPlayer(const Roster& roster);
}  // namespace football::sim

#endif
