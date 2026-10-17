#ifndef FOOTBALL_SIM_EVENT_TOUCH_RECORD_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_RECORD_HPP

#include <cstdint>

#include "football/player/player_definition.hpp"
#include "event/touch_type.hpp"

namespace football::sim::event {

// Read-only diagnostic capture of one accepted touch, recorded where the fact
// was produced. It is never fed back to physics, rules or the RNG; it exists so
// shadow analysis can attribute a touch to its exact executed step and source.
struct RecordedTouch {
  std::uint64_t step_index = 0;
  std::uint64_t generation = 0;
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  e_TouchType type = e_TouchType_None;
  int action_type = 0;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_TOUCH_RECORD_HPP