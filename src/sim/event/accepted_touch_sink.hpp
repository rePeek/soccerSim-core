#ifndef FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_SINK_HPP
#define FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_SINK_HPP

#include "sim/event/accepted_touch.hpp"

namespace football::sim {

// Thin synchronous callback for one accepted touch. It replaces the generic
// SimulationFact/Sink distribution while keeping the exact producer->consumer
// order; no tick buffer, stamps or variants are involved. Transitional only.
class AcceptedTouchSink {
 public:
  virtual ~AcceptedTouchSink() = default;
  virtual void OnAcceptedTouch(const football::sim::event::AcceptedTouch& touch) = 0;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_SINK_HPP