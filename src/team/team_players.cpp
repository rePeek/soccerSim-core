// Entity half of football::team::Team: the only owner of a team's actual
// Player objects. It is compiled into the football_team archive, which links
// football::player; the dependency direction is Team -> Player only.
//
// Minimal Actor surface: Team reads only Player::IsActive() and
// Player::GetFormationEntry().role. A future cross-module Actor API would
// need to expose no more than those two queries.

#include "football/team/team.hpp"

#include <stdexcept>
#include <utility>

#include "player/player.hpp"

namespace football::team {

Team::Team(TeamDefinition definition) : definition_(std::move(definition)) {}

// Defined here so unique_ptr<Player> is destroyed with the complete type.
Team::~Team() = default;

void Team::AddPlayer(std::unique_ptr<Player> player) {
  if (!player) throw std::invalid_argument("team cannot own a null player");
  // Reserve both containers before either is mutated so a later allocation
  // failure cannot leave an owned Player missing from the ordered view.
  owned_.reserve(owned_.size() + 1);
  ordered_.reserve(ordered_.size() + 1);
  owned_.push_back(std::move(player));
  ordered_.push_back(owned_.back().get());
}

void Team::ClearPlayers() noexcept {
  // Drop the raw views first, then destroy the owners. Never dereferences a
  // pointer that is already gone.
  ordered_.clear();
  owned_.clear();
}

void Team::GetActivePlayers(std::vector<Player*>& output) const {
  for (Player* player : ordered_) {
    if (player->IsActive()) output.push_back(player);
  }
}

int Team::ActivePlayerCount() const {
  int count = 0;
  for (Player* player : ordered_) {
    if (player->IsActive()) count++;
  }
  return count;
}

Player* Team::Goalie() const {
  for (Player* player : ordered_) {
    if (player->IsActive() && player->GetFormationEntry().role == e_PlayerRole_GK)
      return player;
  }
  return nullptr;
}

}  // namespace football::team
