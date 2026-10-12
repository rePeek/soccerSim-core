#ifndef FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP
#define FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "sim/player/player_definition.hpp"
#include "sim/runtime/match_config.hpp"
#include "sim/event/touch_type.hpp"

namespace football::sim::event {

// Transitional carrier for one accepted touch. It keeps the current
// synchronous producer->consumer order (touch bookkeeping, event recognition,
// referee) without the generic SimulationFact/StampedFact/TickFactBuffer
// distribution machinery. Not persisted in Snapshots and not the final
// architecture.
struct AcceptedTouch {
  football::sim::Tick touched_at{};
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  e_TouchType type = e_TouchType_None;
  football::math::Vector3 ball_position;
  football::math::Vector3 ball_velocity;
  int action_type = 0;
  // Reviewed CCD deflection, not deliberate opponent play. Default false keeps
  // the legacy producer's semantics until its replacement is explicitly enabled.
  bool preserve_opponent_offside = false;
  // Authorized selected kick action plus genuine approaching boot CCD impact.
  // Physical timing replaces the animation marker only for this reviewed path.
  bool physical_kick = false;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP