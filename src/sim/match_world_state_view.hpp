#ifndef FOOTBALL_SIM_MATCH_WORLD_STATE_VIEW_HPP
#define FOOTBALL_SIM_MATCH_WORLD_STATE_VIEW_HPP

#include <vector>

#include "state/world_state.hpp"

class Match;

// Snapshot assembled at the decision boundary. Its data is copied so controls
// cannot retain references into mutable simulation objects.
class MatchWorldStateView final : public WorldStateView {
 public:
  explicit MatchWorldStateView(Match& match);

  std::uint64_t tick() const override { return tick_; }
  blunted::Vector3 ball_position() const override { return ball_position_; }
  std::span<const WorldPlayerState> players() const override { return players_; }

 private:
  std::uint64_t tick_ = 0;
  blunted::Vector3 ball_position_ = blunted::Vector3(0);
  std::vector<WorldPlayerState> players_;
};

#endif  // FOOTBALL_SIM_MATCH_WORLD_STATE_VIEW_HPP
