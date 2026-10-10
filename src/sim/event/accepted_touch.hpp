#ifndef FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP
#define FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/player.hpp"
#include "model/team.hpp"
#include "sim/event/touch_type.hpp"

namespace football::sim::event {

// Transitional carrier for one accepted touch. It keeps the current
// synchronous producer->consumer order (touch bookkeeping, event recognition,
// referee) without the generic SimulationFact/StampedFact/TickFactBuffer
// distribution machinery. Not persisted in Snapshots and not the final
// architecture.
struct AcceptedTouch {
  football::sim::Tick touched_at{};
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  football::model::TeamSide team = football::model::TeamSide::Home;
  e_TouchType type = e_TouchType_None;
  blunted::Vector3 ball_position;
  blunted::Vector3 ball_velocity;
  int action_type = 0;
  // Reviewed CCD deflection, not deliberate opponent play. Default false keeps
  // the legacy producer's semantics until its replacement is explicitly enabled.
  bool preserve_opponent_offside = false;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP