#ifndef FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP

#include <span>

#include "sim/runtime/match_config.hpp"
#include "sim/runtime/team_state.hpp"

class Player;

namespace football::sim {

// Borrowed, non-owning view of one team for a single call (rules, observation).
// Runtime reconstructs it every tick and never hands over ownership, so it is
// not a Team entity: it carries only identity, direction, the roster slice and a
// pointer to the live runtime-owned TeamMatchState. Nothing in this header can
// create or own a team.
struct TeamView {
  int team_id = -1;
  TeamSide side = TeamSide::Home;
  int dynamic_side = 0;
  int static_side = 0;
  Player* goalie = nullptr;
  // Full roster in stable insertion order; callers filter IsActive() themselves.
  std::span<Player* const> players;
  // Live runtime-owned match facts; null only for a view without team state.
  TeamMatchState* state = nullptr;

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
  void SetFadingPossessionAmount(float value) const {
    if (state != nullptr) state->fading_possession_amount = value;
  }
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP
