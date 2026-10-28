#pragma once

#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "football/player/state/player_definition.hpp"
#include "football/team/formation.hpp"
#include "football/team/formation_preset.hpp"
#include "football/team/leadership.hpp"
#include "football/team/lineup.hpp"
#include "football/team/set_piece_orders.hpp"

// The concrete match actor. Team owns Players; it never reads match rules,
// physics, the clock or the Referee, and Player never points back at a Team.
class Player;

namespace football::team {

// Static, copyable team-domain configuration: identity, declared personnel,
// appointments, reusable restart orders and formation templates.
//
// It deliberately owns no Player, Roster, physics, match state, Referee or AI
// controller. Runtime builds one and hands it to the owning Team. It is the
// only writable static configuration; `initial_tactics` is bootstrap input for
// the AI board, never a second writable TacticalBoard.
struct TeamDefinition {
  std::string name;
  std::vector<football::player::PlayerDefinition> players;

  Lineup lineup;
  Leadership leadership;
  SetPieceOrders set_piece_orders;
  FormationPresets formations;

  InitialFormation initial_formation;
  TacticalFormation tactical_formation;
  std::map<std::string, float> initial_tactics;
};

// Pure validation; throws std::invalid_argument. Player objects/state are not
// involved, and no RNG is consumed.
void ValidateTeamDefinition(const TeamDefinition& team);
void ValidateTeamDefinitions(const TeamDefinition& home,
                             const TeamDefinition& away);

// Entity owning one team's actual Player objects and their stable order.
//
// Exactly one Team owns each Player: `owned_` is the only ownership container
// and `ordered_` holds non-owning views in declaration/insertion order. The
// ordered view is never re-sorted, and a sent-off player stays in it until the
// Team is destroyed. Copying and moving are deleted so a bound Player's owner
// address is stable for the whole match. Match state (possession, frames,
// active lineup) stays with Runtime.
//
// The definition is bound once at construction; it keeps the static templates
// that Runtime reads while the actors are alive. Player copies its own
// PlayerDefinition by value, so the Team's definition storage is never borrowed.
//
// Implementation note: the Player-owning members are defined in
// `src/team/team_players.cpp`, compiled into the football_team archive, which
// links football::player. Runtime composes Teams but owns no Player itself.
class Team final {
 public:
  explicit Team(TeamDefinition definition = {});
  ~Team();

  Team(const Team&) = delete;
  Team& operator=(const Team&) = delete;
  Team(Team&&) = delete;
  Team& operator=(Team&&) = delete;

  const TeamDefinition& Definition() const noexcept { return definition_; }

  // Stable, non-owning view in insertion order.
  std::span<Player* const> Players() const noexcept { return ordered_; }

  // Takes ownership. Rejects null. Strong for the ordered/owned invariant: a
  // failed insertion never leaves an owned Player missing from Players().
  void AddPlayer(std::unique_ptr<Player> player);
  // Destroys every owned Player. Used only by Runtime teardown, after Exit.
  void ClearPlayers() noexcept;

  void GetActivePlayers(std::vector<Player*>& output) const;
  int ActivePlayerCount() const;
  // Active goalkeeper, or null when none is active.
  Player* Goalie() const;

 private:
  TeamDefinition definition_;
  // Ownership; raw views below are stable for the whole Team lifetime.
  std::vector<std::unique_ptr<Player>> owned_;
  std::vector<Player*> ordered_;
};

}  // namespace football::team
