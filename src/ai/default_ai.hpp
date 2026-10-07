#ifndef FOOTBALL_AI_DEFAULT_AI_HPP
#define FOOTBALL_AI_DEFAULT_AI_HPP

#include <array>
#include <utility>

#include "ai/ai_config.hpp"
#include "ai/tactical_board.hpp"
#include "sim/player_control_set.hpp"
#include "sim/world_state.hpp"

namespace football::ai {

// Owns persistent tactics, never actors, transient input requests or RNG.
// Update replaces frame-local output without mutating tactical intent.
class DefaultAI {
 public:
  DefaultAI() { boards_[1].side = model::TeamSide::Away; }
  explicit DefaultAI(std::array<TacticalBoard, 2> boards)
      : boards_(std::move(boards)) {
    boards_[0].side = model::TeamSide::Home;
    boards_[1].side = model::TeamSide::Away;
  }
  DefaultAI(const model::Team &home, const model::Team &away,
            const model::Pitch &pitch = model::MakeLegacyPitch(),
            const AIConfig &config = {})
      : boards_{config.initial_tactics[0]
                    ? *config.initial_tactics[0] : MakeTacticalBoard(home, model::TeamSide::Home, pitch),
                config.initial_tactics[1]
                    ? *config.initial_tactics[1] : MakeTacticalBoard(away, model::TeamSide::Away, pitch)} {
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
