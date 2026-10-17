#ifndef FOOTBALL_AI_DEFAULT_AI_HPP
#define FOOTBALL_AI_DEFAULT_AI_HPP

#include <array>
#include "football/pitch/pitch.hpp"
#include <utility>

#include "football/ai/ai_config.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/player/player_control_set.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// Owns persistent tactics, never actors, transient input requests or RNG.
// Update replaces frame-local output without mutating tactical intent.
class DefaultAI {
 public:
  DefaultAI() { boards_[1].side = football::sim::TeamSide::Away; }
  explicit DefaultAI(std::array<TacticalBoard, 2> boards)
      : boards_(std::move(boards)) {
    boards_[0].side = football::sim::TeamSide::Home;
    boards_[1].side = football::sim::TeamSide::Away;
  }
  DefaultAI(const football::sim::TeamSetup &home, const football::sim::TeamSetup &away,
            const football::pitch::Pitch &pitch = football::pitch::Pitch{},
            const AIConfig &config = {})
      : boards_{config.initial_tactics[0]
                    ? *config.initial_tactics[0] : MakeTacticalBoard(home, football::sim::TeamSide::Home, pitch),
                config.initial_tactics[1]
                    ? *config.initial_tactics[1] : MakeTacticalBoard(away, football::sim::TeamSide::Away, pitch)} {
    boards_[0].side = football::sim::TeamSide::Home;
    boards_[1].side = football::sim::TeamSide::Away;
  }

  TacticalBoard &tactics(football::sim::TeamSide side) {
    return boards_.at(static_cast<unsigned>(side));
  }
  const TacticalBoard &tactics(football::sim::TeamSide side) const {
    return boards_.at(static_cast<unsigned>(side));
  }
  void Update(const WorldState &world, PlayerControlSet &output) const;

 private:
  std::array<TacticalBoard, 2> boards_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_DEFAULT_AI_HPP
