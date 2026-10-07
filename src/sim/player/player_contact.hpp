#ifndef FOOTBALL_SIM_PLAYER_CONTACT_HPP
#define FOOTBALL_SIM_PLAYER_CONTACT_HPP

#include <span>

class Ball;
class Player;
class Referee;

namespace football::sim {

struct PlayerContactInputs {
  // Active players in processing-roster order, sharing the ball's physical frame.
  std::span<Player* const> players;
  const Ball& ball;
  Player* designated_possession_player;
};

// Mutates players in pair order, then applies accumulated movement sharing.
// TripMe and TripNotice stay synchronous: rules read the just-mutated positions.
// Referee is a transitional rules dependency, not an event queue or context.
void ResolvePlayerContacts(const PlayerContactInputs& inputs, Referee& referee);

}  // namespace football::sim

#endif
