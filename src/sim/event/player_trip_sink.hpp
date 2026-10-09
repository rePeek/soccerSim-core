#ifndef FOOTBALL_SIM_EVENT_PLAYER_TRIP_SINK_HPP
#define FOOTBALL_SIM_EVENT_PLAYER_TRIP_SINK_HPP

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"

class Player;

namespace football::sim {

// Write-only physical-consequence port for player-pair contacts. The contact
// solver reports that an actor fell; it never classifies the event as a foul.
// Consumers translate the report into a PlayerTripFact for the referee.
class PlayerTripSink {
 public:
  virtual ~PlayerTripSink() = default;
  virtual void OnPlayerTripped(Player* victim, Player* offender, int trip_type,
                               football::sim::Tick now,
                               const blunted::Vector3& ball_position) = 0;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_EVENT_PLAYER_TRIP_SINK_HPP
