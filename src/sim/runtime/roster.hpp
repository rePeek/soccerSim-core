// Roster owns one team's player entities and their stable order. It knows
// nothing about tactics, possession, collisions or the match clock: those stay
// with Runtime, AI and Rules. Keeping ownership here is what lets Player stop
// holding a Team* (see player_tick_context.hpp) and lets Team stop being an
// entity-owning class.

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
