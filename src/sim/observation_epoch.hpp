#ifndef FOOTBALL_SIM_OBSERVATION_EPOCH_HPP
#define FOOTBALL_SIM_OBSERVATION_EPOCH_HPP

#include <memory>

// Opaque, owning lifetime identity, not an actor/runtime handle or numeric ID.
// A new Match creates one marker; copies retain it, so allocator reuse cannot
// alias an old epoch. No global counter, wall clock or RNG is involved.
// Empty means an unbound synthetic observation. Not a durable serialization ID.
class ObservationEpoch {
 public:
  ObservationEpoch() = default;
  static ObservationEpoch New() {
    ObservationEpoch epoch;
    epoch.identity_ = std::make_shared<const Identity>();
    return epoch;
  }
  bool valid() const { return bool(identity_); }
  bool operator==(const ObservationEpoch &) const = default;

 private:
  struct Identity {};
  std::shared_ptr<const Identity> identity_;
};

#endif  // FOOTBALL_SIM_OBSERVATION_EPOCH_HPP
