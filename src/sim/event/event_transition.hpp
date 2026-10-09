#ifndef FOOTBALL_SIM_EVENT_EVENT_TRANSITION_HPP
#define FOOTBALL_SIM_EVENT_EVENT_TRANSITION_HPP

#include <variant>

#include "sim/event/event.hpp"

namespace football::sim::event {

// Immediate, rule-relevant notifications emitted while a behavior is being
// recognized. A transition marks a change the referee may act on at once; it is
// not the permanent MatchEvent history. Keeping this separate lets the referee
// relate rules over time without owning the event lifecycle.

struct PassStarted {
  EventId id = kInvalidEventId;
  model::PlayerId passer = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  Tick tick{};
};

struct PassEnded {
  EventId id = kInvalidEventId;
  EventStatus status = EventStatus::Active;
  model::PlayerId passer = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  model::PlayerId receiver = model::kInvalidPlayerId;
  Tick tick{};
};

struct ShotStarted {
  EventId id = kInvalidEventId;
  model::PlayerId shooter = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  Tick tick{};
};

struct ShotEnded {
  EventId id = kInvalidEventId;
  EventStatus status = EventStatus::Active;
  model::PlayerId shooter = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  bool goal = false;
  Tick tick{};
};

using EventTransition =
    std::variant<PassStarted, PassEnded, ShotStarted, ShotEnded>;

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_EVENT_TRANSITION_HPP
