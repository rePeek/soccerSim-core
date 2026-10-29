#ifndef FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP

#include <algorithm>
#include <span>

#include "football/player/execution/player_ball_relation.hpp"
#include "football/team/formation_entry.hpp"
#include "football/runtime/match_config.hpp"
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
  // The end this team defends in the Stadium Frame: -1 (-X) or +1 (+X).
  int defending_direction = -1;
  Player* goalie = nullptr;
  // Full player list in stable insertion order; callers filter IsActive() themselves.
  std::span<Player* const> players;
  // Runtime-owned formation, one entry per player in the same order as
  // `players`. Borrowed for the tick; Player no longer holds this data.
  std::span<const FormationEntry> formation;
  // Runtime-owned ball relations, aligned with `players`.
  std::span<const football::player::PlayerBallRelation> ball_relations;
  // Live runtime-owned state; null only for a view without team state.
  TeamMatchState* state = nullptr;

  // The direction this team attacks in the Stadium Frame.
  int attack_direction() const { return -defending_direction; }

  bool HasPossession() const { return state != nullptr && state->has_possession; }
  int TimeNeededToGetToBall_ms() const {
    return state != nullptr ? state->time_to_ball_ms : 100;
  }
  Player* DesignatedPlayer() const {
    return state != nullptr ? state->designated_actor : nullptr;
  }
  void SetDesignatedPlayer(Player* player) const {
    if (state != nullptr) state->SetDesignatedPlayer(player);
  }
  float fading_possession_amount() const {
    return state != nullptr ? state->fading_possession_amount : 0.0f;
  }
  // Restart placement weight. Runtime-owned: the write is immediately visible
  // to any consumer that borrows this state, so there is no publication step.
  void SetFadingPossessionAmount(float value) const {
    if (state == nullptr) return;
    state->fading_possession_amount = std::clamp(value, 0.5f, 1.5f);
  }
};

// The formation entry for one actor, looked up by the same stable order as
// `players`. Returns nullptr when the actor is not part of this view, so
// callers keep the existing default-formation fallback.
inline const FormationEntry* FormationEntryFor(const TeamView& view,
                                                const Player* player) {
  for (std::size_t i = 0; i < view.players.size(); ++i) {
    if (view.players[i] == player) return &view.formation[i];
  }
  return nullptr;
}

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_VIEW_HPP
