#ifndef FOOTBALL_AI_COACH_CONTEXT_HPP
#define FOOTBALL_AI_COACH_CONTEXT_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "football/ai/shout.hpp"
#include "football/ai/tactical_plan.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// The read-only view a coach turn reasons over. It is derived from WorldState
// plus the coach's own plan and active shouts; it never exposes the opponent's
// private board and never invents a fact the observation cannot supply (no xG,
// injuries or hidden personality in v1).

// Coach-facing event vocabulary. Formal confirmed MatchEvents map onto these;
// BallOutOfPlay is the WorldState-delta fallback for synthetic observations.
enum class CoachEventType : std::uint8_t {
  KickOff,
  Goal,
  Card,
  Foul,
  Offside,
  Restart,
  BallOutOfPlay,
  BallInPlay,
  PeriodChange,
};

struct MatchEventSummary {
  CoachEventType type = CoachEventType::KickOff;
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  std::uint64_t tick = 0;
  e_GameMode restart = e_GameMode_Normal;
};

struct MatchClockSummary {
  std::uint64_t regulation_ticks = 0;
  bool half_underway = false;
  bool ball_in_play = false;
};

// Aggregated over a bounded rolling window. It holds only what the collector
// actually sampled; an empty window reports zero samples rather than a guess.
struct TeamTrendSummary {
  int sample_count = 0;
  float possession_fraction = 0.0f;  // self possession samples / samples
  float mean_ball_x_fraction = 0.5f; // canonical 0 own goal .. 1 opponent goal
  float ball_in_play_fraction = 0.0f;
};

struct PlayerSummary {
  football::player::PlayerId id = football::player::kInvalidPlayerId;
  e_PlayerRole role = e_PlayerRole_GK;
  football::math::Vector3 position = football::math::Vector3(0);
  bool active = false;
  bool has_possession = false;
  float fatigue_inv = 1.0f;
};

struct CoachContext {
  football::sim::TeamSide self = football::sim::TeamSide::Home;
  std::uint64_t observed_tick = 0;
  std::uint64_t board_revision = 0;
  MatchPhase phase = MatchPhase::PreMatch;
  int self_score = 0;
  int opponent_score = 0;
  MatchClockSummary clock;
  TeamTrendSummary last_5_min;
  TeamTrendSummary last_15_min;
  std::vector<PlayerSummary> own_players;
  std::vector<MatchEventSummary> recent_events;
  // Own persistent intent only. nullopt for a legacy board.
  std::optional<TacticalPlanV2> current_tactics;
  std::vector<ActiveShout> active_shouts;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_CONTEXT_HPP
