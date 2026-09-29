#ifndef FOOTBALL_CORE_MODEL_WORLD_HPP
#define FOOTBALL_CORE_MODEL_WORLD_HPP

#include <cstdint>
#include <utility>
#include <vector>

#include "core/model/ball/ball.hpp"
#include "core/model/player/player.hpp"

namespace football::model {

// Headless simulation authority. A copy of World is a complete prediction
// branch: Ball and Player each copy their immutable attributes and current
// state, with no Match/Humanoid/animation pointers hidden inside.
class World {
 public:
  World(Ball ball, std::vector<Player> players)
      : ball_(std::move(ball)), players_(std::move(players)) {}

  Ball& GetBall() { return ball_; }
  const Ball& GetBall() const { return ball_; }

  std::vector<Player>& Players() { return players_; }
  const std::vector<Player>& Players() const { return players_; }

  std::uint64_t Tick() const { return tick_; }
  void AdvanceTick() { ++tick_; }

 private:
  Ball ball_;
  std::vector<Player> players_;
  std::uint64_t tick_ = 0;
};

}  // namespace football::model

#endif  // FOOTBALL_CORE_MODEL_WORLD_HPP
