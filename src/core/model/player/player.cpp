#include "player.hpp"

namespace football::model {

Player::Player(const PlayerProfile& profile, PlayerState& state)
    : profile_(profile), state_(state) {}

}  // namespace football::model