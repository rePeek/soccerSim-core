#ifndef FOOTBALL_AI_DEFAULT_AI_HPP
#define FOOTBALL_AI_DEFAULT_AI_HPP

#include <array>
#include <utility>

#include "ai/tactical_board.hpp"
#include "control/player_control_set.hpp"
#include "observation/world_state.hpp"

namespace football::ai {

// Owns persistent tactical intent, not actors, simulation timers or RNG.
// Update replaces frame-local output and leaves the boards unchanged.
class DefaultAI {
 public:
  DefaultAI() { boards_[1].side = model::TeamSide::Away; }
  explicit DefaultAI(std::array<TacticalBoard, 2> boards)
      : boards_(std::move(boards)) {
    boards_[0].side = model::TeamSide::Home;
    boards_[1].side = model::TeamSide::Away;
  }

  TacticalBoard &tactics(model::TeamSide side) {
    return boards_.at(static_cast<unsigned>(side));
  }
  const TacticalBoard &tactics(model::TeamSide side) const {
    return boards_.at(static_cast<unsigned>(side));
  }
  void Update(const WorldState &world, PlayerControlSet &output) const;

 private:
  std::array<TacticalBoard, 2> boards_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_DEFAULT_AI_HPP
