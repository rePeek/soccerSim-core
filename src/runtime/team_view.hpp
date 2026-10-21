#ifndef FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP

#include <algorithm>
#include <span>

#include "football/pitch/pitch_frame.hpp"
#include "football/runtime/match_config.hpp"
#include "runtime/team_facts.hpp"
#include "runtime/team_state.hpp"
#include "runtime/team_facts.hpp"
#include "runtime/team_state.hpp"

class Player;

namespace football::sim {

// Borrowed, non-owning view of Runtime's live team relationship for a single
// call (rules, observation). It carries identity, direction, the player slice
// and pointers to the live TeamOrientation/TeamMatchState; static team::TeamDefinition
// configuration is not copied into it. A view must never outlive the tick.
struct TeamView {
  int team_id = -1;
  TeamSide side = TeamSide::Home;
  // The end this team defends in the Stadium Frame (persistent).
  int defending_direction = -1;
  // The direction of the team's current execution frame (transient).
  int execution_direction = -1;
  // MIR-1: the named defending end behind the ±1 static side, so consumers that
  // need a value transform do not re-interpret the number.
  DefendingEnd defending_end() const { return DefendingEndOf(defending_direction); }
  Player* goalie = nullptr;
  // Full player list in stable insertion order; callers filter IsActive() themselves.
  std::span<Player* const> players;
  // Live runtime-owned state; null only for a view without team state.
  TeamMatchState* state = nullptr;
  // Live orientation, needed to publish team facts after a state write.
  const TeamOrientation* orientation = nullptr;

  bool HasPossession() const { return state != nullptr && state->has_possession; }
  int TimeNeededToGetToBall_ms() const {
    return state != nullptr ? state->time_to_ball_ms : 100;
  }
  Player* DesignatedPlayer() const {
    return state != nullptr ? state->designated_player : nullptr;
  }
  void SetDesignatedPlayer(Player* player) const {
    if (state != nullptr) state->designated_player = player;
  }
  float fading_possession_amount() const {
    return state != nullptr ? state->fading_possession_amount : 0.0f;
  }
  // Restart placement weight. The legacy Team setter clamped the value AND
  // published the new facts immediately; both halves are behaviour (Player
  // caches the projection), so they stay together here.
  void SetFadingPossessionAmount(float value) const {
    if (state == nullptr) return;
    state->fading_possession_amount = std::clamp(value, 0.5f, 1.5f);
    if (orientation != nullptr) PublishTeamFacts(players, *orientation, *state);
  }
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP
