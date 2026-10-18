// Roster owns one team's Player entities and their stable order. It is the
// private storage of the Runtime Team entity (TEAM-5.2); it knows nothing about
// tactics, possession, collisions or the match clock. Player therefore holds no
// Team pointer or borrowed definition storage.

#ifndef FOOTBALL_SIM_RUNTIME_ROSTER_HPP
#define FOOTBALL_SIM_RUNTIME_ROSTER_HPP

#include <memory>
#include <span>
#include <vector>

class Player;

namespace football::sim {

class Roster {
 public:
  Roster() = default;
  Roster(const Roster&) = delete;
  Roster& operator=(const Roster&) = delete;

  // Stable, non-owning view in insertion order.
  std::span<Player *const> Players() const { return ordered_; }
  void Add(std::unique_ptr<Player> player);
  void Clear();

  void GetActivePlayers(std::vector<Player *> &out) const;
  int ActivePlayerCount() const;
  // Active goalkeeper, or null when none is active.
  Player *Goalie() const;

 private:
  // Ownership; raw views below are stable for the whole roster lifetime.
  std::vector<std::unique_ptr<Player>> owned_;
  std::vector<Player *> ordered_;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_ROSTER_HPP
