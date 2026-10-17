#ifndef FOOTBALL_SIM_EVENT_EVENT_TRANSITION_HPP
#define FOOTBALL_SIM_EVENT_EVENT_TRANSITION_HPP

#include <variant>

#include "football/event/event.hpp"

namespace football::sim::event {

// Immediate, rule-relevant notifications emitted while a behavior is being
// recognized. A transition marks a change the referee may act on at once; it is
// not the permanent MatchEvent history. Keeping this separate lets the referee
// relate rules over time without owning the event lifecycle.

struct PassStarted {
  EventId id = kInvalidEventId;
  football::player::PlayerId passer = football::player::kInvalidPlayerId;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  Tick tick{};
};

struct PassEnded {
  EventId id = kInvalidEventId;
  EventStatus status = EventStatus::Active;
  football::player::PlayerId passer = football::player::kInvalidPlayerId;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::player::PlayerId receiver = football::player::kInvalidPlayerId;
  Tick tick{};
};

struct ShotStarted {
  EventId id = kInvalidEventId;
  football::player::PlayerId shooter = football::player::kInvalidPlayerId;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  Tick tick{};
};

struct ShotEnded {
  EventId id = kInvalidEventId;
  EventStatus status = EventStatus::Active;
  football::player::PlayerId shooter = football::player::kInvalidPlayerId;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  bool goal = false;
  Tick tick{};
};

using EventTransition =
    std::variant<PassStarted, PassEnded, ShotStarted, ShotEnded>;

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_EVENT_TRANSITION_HPP
