#ifndef FOOTBALL_AI_DEFAULT_AI_HPP
#define FOOTBALL_AI_DEFAULT_AI_HPP

#include <array>
#include <utility>

#include "ai/tactical_board.hpp"
#include "ai/team_requests.hpp"
#include "sim/player_control_set.hpp"
#include "sim/world_state.hpp"

namespace football::ai {

// Owns persistent tactics and explicit transient decision requests, never actors/RNG.
// Update replaces frame-local output without mutating either kind of intent.
class DefaultAI {
 public:
  DefaultAI() { boards_[1].side = model::TeamSide::Away; }
  explicit DefaultAI(std::array<TacticalBoard, 2> boards)
      : boards_(std::move(boards)) {
    boards_[0].side = model::TeamSide::Home;
    boards_[1].side = model::TeamSide::Away;
  }
  DefaultAI(const model::Team &home, const model::Team &away,
            const model::Pitch &pitch = model::MakeLegacyPitch())
      : boards_{MakeTacticalBoard(home, model::TeamSide::Home, pitch),
                MakeTacticalBoard(away, model::TeamSide::Away, pitch)} {}

  TacticalBoard &tactics(model::TeamSide side) {
    return boards_.at(static_cast<unsigned>(side));
  }
  const TacticalBoard &tactics(model::TeamSide side) const {
    return boards_.at(static_cast<unsigned>(side));
  }
  void Update(const WorldState &world, PlayerControlSet &output) const;

  // Requests use only snapshots/IDs. False means no eligible target was found.
  bool RequestAttackingRun(model::TeamSide side, const WorldState &world,
                           std::optional<model::PlayerId> runner = std::nullopt);
  bool RequestTeamPressure(model::TeamSide side, const WorldState &world,
                           std::optional<model::PlayerId> excluded = std::nullopt);
  bool RequestKeeperRush(model::TeamSide side, const WorldState &world);
  const TeamRequests &requests(model::TeamSide side) const {
    return requests_.at(static_cast<unsigned>(side));
  }
  // On a new match/reset, clear transient requests but preserve tactical boards.
  void ResetRequests() { requests_ = {}; }

 private:
  std::array<TacticalBoard, 2> boards_;
  std::array<TeamRequests, 2> requests_{};
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_DEFAULT_AI_HPP
