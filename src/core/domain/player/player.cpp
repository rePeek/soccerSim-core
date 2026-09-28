#include "player.hpp"

namespace football::domain {

Player::Player(const PlayerProfile& profile, PlayerState& state)
    : profile_(profile), state_(state) {}

}  // namespace football::domain