#include "sim/runtime/roster.hpp"

#include "player/player.hpp"

namespace football::sim {

void Roster::Add(std::unique_ptr<Player> player) {
  ordered_.push_back(player.get());
  owned_.push_back(std::move(player));
}

void Roster::Clear() {
  owned_.clear();
  ordered_.clear();
}

void Roster::GetActivePlayers(std::vector<Player *> &out) const {
  for (Player *player : ordered_) {
    if (player->IsActive()) out.push_back(player);
  }
}

int Roster::ActivePlayerCount() const {
  int count = 0;
  for (Player *player : ordered_) {
    if (player->IsActive()) count++;
  }
  return count;
}

Player *Roster::Goalie() const {
  for (Player *player : ordered_) {
    if (player->IsActive() && player->GetFormationEntry().role == e_PlayerRole_GK)
      return player;
  }
  return nullptr;
}

}  // namespace football::sim
