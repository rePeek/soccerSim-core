#ifndef FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP
#define FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP

#include <cstdint>
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
  bool red = false;  // Includes dismissal following a second yellow.
  bool second_yellow = false;
};

struct RestartAwardedEvent {
  Tick tick{};
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  e_GameMode kind = e_GameMode_Normal;
  football::math::Vector3 position;
  // Match-scoped restart identity, shared with RestartTakenEvent.
  std::uint64_t restart_id = 0;
};

// A restart was actually taken: the rules confirmed a legal release. This is
// distinct from RestartAwardedEvent (granted) and never fires for mere setup
// or player readiness.
struct RestartTakenEvent {
  Tick tick{};
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  e_GameMode kind = e_GameMode_Normal;
  football::player::PlayerId taker = football::player::kInvalidPlayerId;
  std::uint64_t restart_id = 0;
};

// The referee confirmed a foul. `team` is the offender's side; the restart
// that follows is recorded separately so a foul is never inferred backwards
// from an arbitrary free-kick award.
struct FoulConfirmedEvent {
  Tick tick{};
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::player::PlayerId offender = football::player::kInvalidPlayerId;
  football::player::PlayerId victim = football::player::kInvalidPlayerId;
};

// The referee confirmed offside. `team` is the flagged attacking side.
struct OffsideConfirmedEvent {
  Tick tick{};
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::player::PlayerId player = football::player::kInvalidPlayerId;
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
  // Complete terminal status: Completed (with goal), Failed or Cancelled.
  // `goal` is only meaningful together with status == Completed.
  EventStatus status = EventStatus::Active;
  bool goal = false;
};

struct PeriodEndedEvent {
  Tick tick{};
  MatchPhase phase = MatchPhase::PreMatch;
};

using MatchEvent = std::variant<GoalScoredEvent, CardShownEvent, RestartAwardedEvent,
    RestartTakenEvent, FoulConfirmedEvent, OffsideConfirmedEvent, PeriodEndedEvent,
    PassCompletedEvent, ShotEndedEvent>;

// One confirmed event with its monotonic, match-scoped sequence number. The
// sequence is assigned by Runtime at publication and never reused.
struct MatchEventRecord {
  std::uint64_t sequence = 0;
  MatchEvent event;
};

// Incremental read cursor for MatchEvents. `generation` identifies one match
// instance, so a cursor from an earlier match is rebased to the first event of
// the new match instead of misreading it. `next_sequence` is the last sequence
// the consumer has observed; 0 means "nothing consumed yet".
struct MatchEventCursor {
  std::uint64_t generation = 0;
  std::uint64_t next_sequence = 0;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_MATCH_EVENT_HPP
