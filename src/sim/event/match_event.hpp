#ifndef FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP
#define FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP

#include <variant>

#include "football/foundation/time/tick.hpp"
#include "football/player/player_role.hpp"
#include "football/referee/game_mode.hpp"
#include "football/player/player_definition.hpp"
#include "football/runtime/match_config.hpp"
#include "football/event/event.hpp"
#include "football/runtime/phase.hpp"

namespace football::sim::event {

// Confirmed match history, written only after Simulation has applied a ruling.
// Values are self-contained so a log survives actor/owner teardown and can be
// queried by presentation, statistics or training-data consumers.
struct GoalScoredEvent {
  Tick tick{};
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::player::PlayerId scorer = football::player::kInvalidPlayerId;
  bool own_goal = false;
};

struct CardShownEvent {
  Tick tick{};
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  bool red = false;
};

struct RestartAwardedEvent {
  Tick tick{};
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  e_GameMode kind = e_GameMode_Normal;
  football::math::Vector3 position;
};

struct PassCompletedEvent {
  Tick tick{};
  EventId id = kInvalidEventId;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::player::PlayerId passer = football::player::kInvalidPlayerId;
  football::player::PlayerId receiver = football::player::kInvalidPlayerId;
};

struct ShotEndedEvent {
  Tick tick{};
  EventId id = kInvalidEventId;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::player::PlayerId shooter = football::player::kInvalidPlayerId;
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
