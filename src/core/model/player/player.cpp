#include "player.hpp"

#include <cassert>

namespace football::model {

Player::Player(PlayerId id, float height, float mass, float bodyRadius,
               float strength, float balance)
    : id_(id),
      height_(height),
      mass_(mass),
      bodyRadius_(bodyRadius),
      strength_(strength),
      balance_(balance) {
  assert(id_ != kInvalidPlayerId);
  assert(height_ > 0.0f);
  assert(mass_ > 0.0f);
  assert(bodyRadius_ > 0.0f);
}

}  // namespace football::model
