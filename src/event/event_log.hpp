#ifndef FOOTBALL_SIM_EVENT_EVENT_LOG_HPP
#define FOOTBALL_SIM_EVENT_EVENT_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "football/event/match_event.hpp"

namespace football::sim::event {

// Owned, append-only match history. Never an authoritative state store and never
// queried by physics or the referee: only confirmed outcomes are recorded.
//
// Records carry a monotonic match-scoped sequence so an external consumer can
// read new events incrementally with a MatchEventCursor. Clear() starts a new
// match generation, so a stale cursor is rebased rather than misread.
class EventLog {
 public:
  void Record(MatchEvent event) {
    events_.push_back(MatchEventRecord{++next_sequence_, std::move(event)});
  }

  // Match lifecycle boundary: starts a new generation and drops old events.
  void Clear() {
    events_.clear();
    next_sequence_ = 0;
    ++generation_;
  }

  std::size_t size() const { return events_.size(); }
  bool empty() const { return events_.empty(); }
  const std::vector<MatchEventRecord>& events() const { return events_; }
  std::uint64_t generation() const { return generation_; }

  // Owning, sequence-ordered increment of every confirmed event after `cursor`.
  // A cursor from another match generation is rebased to this one. Read-only
  // with respect to rules and physics; it only advances the cursor.
  std::vector<MatchEventRecord> EventsSince(MatchEventCursor& cursor) const {
    if (cursor.generation != generation_) {
      cursor.generation = generation_;
      cursor.next_sequence = 0;
    }
    std::vector<MatchEventRecord> out;
    for (const MatchEventRecord& record : events_) {
      if (record.sequence > cursor.next_sequence) out.push_back(record);
    }
    if (!out.empty()) cursor.next_sequence = out.back().sequence;
    return out;
  }

 private:
  std::vector<MatchEventRecord> events_;
  std::uint64_t next_sequence_ = 0;
  std::uint64_t generation_ = 1;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_EVENT_LOG_HPP
