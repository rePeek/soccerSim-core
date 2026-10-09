#ifndef FOOTBALL_SIM_EVENT_TICK_FACT_BUFFER_HPP
#define FOOTBALL_SIM_EVENT_TICK_FACT_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "foundation/time/tick.hpp"
#include "sim/fact/simulation_fact.hpp"

namespace football::sim::event {

// One ordered fact stream for the current tick. Touch, interaction and boundary
// facts share a single queue so their relative order is preserved; there are no
// per-kind queues to merge later. The buffer owns no history: BeginTick reuses
// the storage and drops the previous tick.
//
// Facts are stamped with the tick and a monotonically increasing sequence. A
// generation value rejects facts that outlived a situation reset. PopPending
// returns by value so consuming a fact can never invalidate a reference while
// new facts are appended (recursive consumption is a caller policy, not
// something this buffer permits by accident).
class TickFactBuffer {
 public:
  void BeginTick(Tick tick, std::uint64_t generation = 0) {
    tick_ = tick;
    generation_ = generation;
    next_sequence_ = 0;
    cursor_ = 0;
    facts_.clear();
  }

  // Stamps and appends one fact to the tail of the current tick stream.
  std::uint32_t Emit(SimulationFact fact) {
    const std::uint32_t sequence = next_sequence_++;
    facts_.push_back(StampedFact{tick_, generation_, sequence, std::move(fact)});
    return sequence;
  }

  bool HasPending() const { return cursor_ < facts_.size(); }

  // Returns the next unprocessed fact, or nullopt when the batch is drained.
  std::optional<StampedFact> PopPending() {
    if (!HasPending()) return std::nullopt;
    return facts_[cursor_++];
  }

  // Number of facts not yet consumed.
  std::size_t pending_count() const { return facts_.size() - cursor_; }

  Tick tick() const { return tick_; }
  std::uint64_t generation() const { return generation_; }
  std::uint32_t next_sequence() const { return next_sequence_; }
  bool empty() const { return facts_.empty(); }
  const std::vector<StampedFact>& facts() const { return facts_; }

 private:
  Tick tick_{};
  std::uint64_t generation_ = 0;
  std::uint32_t next_sequence_ = 0;
  std::size_t cursor_ = 0;
  std::vector<StampedFact> facts_;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_TICK_FACT_BUFFER_HPP
