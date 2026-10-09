#ifndef FOOTBALL_SIM_EVENT_EVENT_HPP
#define FOOTBALL_SIM_EVENT_EVENT_HPP

#include <cstdint>
#include <optional>

#include "foundation/time/tick.hpp"
#include "model/player.hpp"
#include "model/team.hpp"

namespace football::sim::event {

using EventId = std::uint64_t;
inline constexpr EventId kInvalidEventId = 0;

// Lifecycle of a recognized football behavior. A behavior is opened by an
// accepted action, may stay Active across ticks, and ends in exactly one
// terminal state. Events are recognized behavior, distinct from the physical
// facts that triggered them and from the referee's verdicts.
enum class EventStatus { Active, Completed, Failed, Cancelled };

struct PassEvent {
  EventId id = kInvalidEventId;
  model::PlayerId passer = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  Tick started_at{};
  std::optional<Tick> ended_at;
  EventStatus status = EventStatus::Active;
  std::optional<model::PlayerId> receiver;
};

struct ShotEvent {
  EventId id = kInvalidEventId;
  model::PlayerId shooter = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  Tick started_at{};
  std::optional<Tick> ended_at;
  EventStatus status = EventStatus::Active;
  bool goal = false;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_EVENT_HPP
