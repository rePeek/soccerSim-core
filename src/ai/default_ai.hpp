#ifndef FOOTBALL_AI_DEFAULT_AI_HPP
#define FOOTBALL_AI_DEFAULT_AI_HPP

#include <span>

#include "control/player_control_set.hpp"
#include "control/tactical_board.hpp"
#include "observation/world_state.hpp"

namespace football::ai {

// A value-only coach. Tactical results belong to the caller's board, not to
// actors, the referee, or a controller cached inside Simulation.
void UpdateTactics(const WorldState &world, std::span<TacticalBoard> boards);

// Stateless player policy. Reset/replay require no hidden RNG or actor cache.
// Replaces output each tick; absent/inactive/human-owned actors produce no AI
// command. The composition root applies explicit caller overrides afterwards.
class DefaultAI {
 public:
  void Update(const WorldState &world, std::span<const TacticalBoard> boards,
              PlayerControlSet &output) const;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_DEFAULT_AI_HPP
