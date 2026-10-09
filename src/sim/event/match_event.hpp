#ifndef FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP
#define FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP

#include <variant>

#include "foundation/time/tick.hpp"
#include "model/football_types.hpp"
#include "model/player.hpp"
#include "model/team.hpp"
#include "sim/event/event.hpp"
#include "sim/runtime/phase.hpp"

namespace football::sim::event {

// Confirmed match history, written only after Simulation has applied a ruling.
// Values are self-contained so a log survives actor/owner teardown and can be
// queried by presentation, statistics or training-data consumers.
struct GoalScoredEvent {
  Tick tick{};
  model::TeamSide team = model::TeamSide::Home;
  model::PlayerId scorer = model::kInvalidPlayerId;
  bool own_goal = false;
};

struct CardShownEvent {
  Tick tick{};
  model::TeamSide team = model::TeamSide::Home;
  model::PlayerId player = model::kInvalidPlayerId;
  bool red = false;
};

struct RestartAwardedEvent {
  Tick tick{};
  model::TeamSide team = model::TeamSide::Home;
  e_GameMode kind = e_GameMode_Normal;
  blunted::Vector3 position;
};

struct PassCompletedEvent {
  Tick tick{};
  EventId id = kInvalidEventId;
  model::TeamSide team = model::TeamSide::Home;
  model::PlayerId passer = model::kInvalidPlayerId;
  model::PlayerId receiver = model::kInvalidPlayerId;
};

struct ShotEndedEvent {
  Tick tick{};
  EventId id = kInvalidEventId;
  model::TeamSide team = model::TeamSide::Home;
  model::PlayerId shooter = model::kInvalidPlayerId;
  bool goal = false;
};

struct PeriodEndedEvent {
  Tick tick{};
  MatchPhase phase = MatchPhase::PreMatch;
};

using MatchEvent = std::variant<GoalScoredEvent, CardShownEvent, RestartAwardedEvent,
    PeriodEndedEvent, PassCompletedEvent, ShotEndedEvent>;

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP
