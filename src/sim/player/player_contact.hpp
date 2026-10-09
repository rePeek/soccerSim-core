#ifndef FOOTBALL_SIM_PLAYER_CONTACT_HPP
#define FOOTBALL_SIM_PLAYER_CONTACT_HPP

#include <span>
#include "foundation/time/tick.hpp"
#include "sim/event/player_trip_sink.hpp"

namespace football::ball { class Ball; }
class Player;

namespace football::sim {

struct PlayerContactInputs {
  Tick now;
  // Active players in processing-roster order, sharing the ball's physical frame.
  std::span<Player* const> players;
  const football::ball::Ball& ball;
  Player* designated_possession_player;
  Player* ball_retainer;
};

// Mutates players in pair order, then applies accumulated movement sharing.
// TripMe reports the physical fall through the write-only sink; rule verdicts
// (fouls, cards, advantages) belong to the referee and are never decided here.
void ResolvePlayerContacts(const PlayerContactInputs& inputs, PlayerTripSink& trips);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_CONTACT_HPP
