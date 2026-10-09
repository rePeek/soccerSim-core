#ifndef FOOTBALL_SIM_EVENT_EVENT_LOG_HPP
#define FOOTBALL_SIM_EVENT_EVENT_LOG_HPP

#include <cstddef>
#include <utility>
#include <vector>

#include "sim/event/match_event.hpp"

namespace football::sim::event {

// Owned, append-only match history. Never an authoritative state store and never
// queried by physics or the referee: only confirmed outcomes are recorded.
class EventLog {
 public:
  void Record(MatchEvent event) { events_.push_back(std::move(event)); }
  void Clear() { events_.clear(); }
  std::size_t size() const { return events_.size(); }
  bool empty() const { return events_.empty(); }
  const std::vector<MatchEvent>& events() const { return events_; }

 private:
  std::vector<MatchEvent> events_;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_EVENT_LOG_HPP
