#ifndef FOOTBALL_CONTROL_PLAYER_CONTROL_SET_HPP
#define FOOTBALL_CONTROL_PLAYER_CONTROL_SET_HPP

#include <span>
#include <vector>
#include <utility>

#include "control/player_control.hpp"

class PlayerControlSet {
 public:
  void Set(football::model::PlayerId player, PlayerControl control) {
    control.player = player;
    for (PlayerControl& existing : controls_) {
      if (existing.player == player) {
        existing = std::move(control);
        return;
      }
    }
    controls_.push_back(std::move(control));
  }

  const PlayerControl* Get(football::model::PlayerId player) const {
    for (const PlayerControl& control : controls_) {
      if (control.player == player) return &control;
    }
    return nullptr;
  }

  std::span<const PlayerControl> controls() const { return controls_; }
  void Clear() { controls_.clear(); }

 private:
  std::vector<PlayerControl> controls_;
};

#endif  // FOOTBALL_CONTROL_PLAYER_CONTROL_SET_HPP
