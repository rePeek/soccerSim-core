#ifndef FOOTBALL_SIM_PLAYER_PLAYER_TEAM_VIEW_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_TEAM_VIEW_HPP

#include <span>

#include "football/player/player_team_match_facts.hpp"

class Player;

// Borrowed, non-owning view of one team for a single player call. Runtime
// reconstructs it every tick and never hands over ownership, so it is not a
// Team entity: it exposes only the roster slices and match facts that
// Player actually reads. Replaces the transitional `Team&` in
// PlayerTickContext so player code stops depending on the runtime Team class.
struct PlayerTeamView {
  int team_id = -1;
  std::span<Player* const> teammates;
  std::span<Player* const> opponents;
  Player* goalie = nullptr;
  // Live runtime-owned team state, read-only for actors. Non-null for a real
  // match; a null slot just means "no team match facts", never "no team".
  // Runtime is the only writer; a player action that changes team state goes
  // through PlayerRuntimeSink.
  const football::player::PlayerTeamMatchFacts* state = nullptr;

  bool HasPossession() const {
    return state != nullptr && state->has_possession;
  }
  int TimeNeededToGetToBall_ms() const {
    return state != nullptr ? state->time_to_ball_ms : 100;
  }
  Player* DesignatedPlayer() const {
    return state != nullptr ? state->designated_player : nullptr;
  }
};

// Counts active players in a roster slice. Defined in player_team_view.cpp so
// this header stays free of player.hpp.
int ActivePlayerCount(std::span<Player* const> players);

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_TEAM_VIEW_HPP
