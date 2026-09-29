#ifndef FOOTBALL_CORE_MODEL_PLAYER_HPP
#define FOOTBALL_CORE_MODEL_PLAYER_HPP

#include <cstdint>
#include <limits>

#include "foundation/math/vector3.hpp"

namespace football::model {

using PlayerId = std::uint32_t;
inline constexpr PlayerId kInvalidPlayerId = std::numeric_limits<PlayerId>::max();

// Dynamic movement data. Identity and physical ability deliberately do not
// live here: those are immutable player attributes.
struct PlayerState {
  blunted::Vector3 position = blunted::Vector3(0.0f, 0.0f, 0.0f);
  blunted::Vector3 velocity = blunted::Vector3(0.0f, 0.0f, 0.0f);
  blunted::Vector3 movementFacing = blunted::Vector3(0.0f, -1.0f, 0.0f);
  blunted::Vector3 torsoFacing = blunted::Vector3(0.0f, -1.0f, 0.0f);

  void Mirror() {
    position.Mirror();
    velocity.Mirror();
  }
};

struct PlayerKinematicResult {
  blunted::Vector3 position = blunted::Vector3(0.0f, 0.0f, 0.0f);
  blunted::Vector3 velocity = blunted::Vector3(0.0f, 0.0f, 0.0f);
  blunted::Vector3 movementFacing = blunted::Vector3(0.0f, -1.0f, 0.0f);
  blunted::Vector3 torsoFacing = blunted::Vector3(0.0f, -1.0f, 0.0f);
};

inline float Speed(const PlayerState& state) {
  return state.velocity.GetLength();
}

// Thin simulation model. `id` is an immutable part of the player profile,
// alongside physical dimensions and abilities. State updates are committed as
// one value so physics owns all calculations and Player remains behavior-free.
class Player {
 public:
  Player(PlayerId id, float height = 1.80f, float mass = 75.0f,
         float bodyRadius = 0.36f, float strength = 0.5f,
         float balance = 0.5f);

  PlayerId Id() const { return id_; }
  float Height() const { return height_; }
  float Mass() const { return mass_; }
  float BodyRadius() const { return bodyRadius_; }
  float Strength() const { return strength_; }
  float Balance() const { return balance_; }

  const PlayerState& State() const { return state_; }
  void SetState(const PlayerState& next) { state_ = next; }

  const blunted::Vector3& Position() const { return state_.position; }
  const blunted::Vector3& Velocity() const { return state_.velocity; }
  const blunted::Vector3& MovementFacing() const {
    return state_.movementFacing;
  }
  const blunted::Vector3& TorsoFacing() const { return state_.torsoFacing; }
  float Speed() const { return football::model::Speed(state_); }

 private:
  const PlayerId id_;
  const float height_;
  const float mass_;
  const float bodyRadius_;
  const float strength_;
  const float balance_;
  PlayerState state_;
};

}  // namespace football::model

#endif  // FOOTBALL_CORE_MODEL_PLAYER_HPP
