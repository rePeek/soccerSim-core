#ifndef FOOTBALL_CORE_MODEL_PLAYER_HPP
#define FOOTBALL_CORE_MODEL_PLAYER_HPP

#include <cstdint>
#include <limits>

#include "foundation/math/vector3.hpp"

namespace football_sim {

using PlayerId = std::uint32_t;
inline constexpr PlayerId kInvalidPlayerId = std::numeric_limits<PlayerId>::max();

// Dynamic movement data. Identity and physical ability deliberately do not
// live here: those are immutable player attributes.
struct PlayerState {
  football_sim::math::Vector3 position = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 velocity = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 movementFacing = football_sim::math::Vector3(0.0f, -1.0f, 0.0f);
  football_sim::math::Vector3 torsoFacing = football_sim::math::Vector3(0.0f, -1.0f, 0.0f);

  void Mirror() {
    position.Mirror();
    velocity.Mirror();
  }
};

struct PlayerKinematicResult {
  football_sim::math::Vector3 position = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 velocity = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 movementFacing = football_sim::math::Vector3(0.0f, -1.0f, 0.0f);
  football_sim::math::Vector3 torsoFacing = football_sim::math::Vector3(0.0f, -1.0f, 0.0f);
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

  const football_sim::math::Vector3& Position() const { return state_.position; }
  const football_sim::math::Vector3& Velocity() const { return state_.velocity; }
  const football_sim::math::Vector3& MovementFacing() const {
    return state_.movementFacing;
  }
  const football_sim::math::Vector3& TorsoFacing() const { return state_.torsoFacing; }
  float Speed() const { return football_sim::Speed(state_); }

 private:
  const PlayerId id_;
  const float height_;
  const float mass_;
  const float bodyRadius_;
  const float strength_;
  const float balance_;
  PlayerState state_;
};

}  // namespace football_sim

#endif  // FOOTBALL_CORE_MODEL_PLAYER_HPP
