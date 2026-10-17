#ifndef FOOTBALL_AI_COACH_CONTEXT_COLLECTOR_HPP
#define FOOTBALL_AI_COACH_CONTEXT_COLLECTOR_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "football/ai/coach_context.hpp"
#include "football/ai/coach_command.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/ai/shout.hpp"

namespace football::ai {

// Incrementally folds the observed WorldState into a bounded rolling window and
// a deduplicated event list. It never calls a model and never mutates the
// simulation. Clock comparisons use world.tick / regulation_time explicitly.
class ContextCollector {
 public:
  static constexpr std::uint64_t kSampleIntervalTicks = 500;  // 5 s
  static constexpr std::uint64_t kWindowTicks = 90000;        // 15 min
  static constexpr std::size_t kMaxEvents = 32;
  static constexpr std::size_t kMaxSamples = 256;

  explicit ContextCollector(football::sim::TeamSide self) : self_(self) {}

  // Consumes one observation tick. `shouts` is read-only and stored in the
  // context, never modified.
  void OnTick(const WorldState& world,
              const std::vector<ActiveShout>& shouts);
  void Reset();

  football::sim::TeamSide self() const { return self_; }

  // Builds the read-only context for a coach turn. The board is the coach's own
  // persistent intent; its revision and plan are exposed, never the opponent's.
  CoachContext Build(const WorldState& world, const TacticalBoard& own_board) const;

 private:
  struct Sample {
    std::uint64_t tick = 0;
    bool possession = false;
    float ball_x_fraction = 0.5f;
    bool ball_in_play = false;
  };

  void FoldSample(const WorldState& world);
  void DetectEvents(const WorldState& world);
  TeamTrendSummary Summarise(std::uint64_t window) const;

  football::sim::TeamSide self_;
  std::vector<ActiveShout> shouts_;
  std::deque<Sample> samples_;
  std::vector<MatchEventSummary> events_;
  std::uint64_t last_sample_tick_ = 0;
  bool have_sample_ = false;
  int last_self_score_ = 0;
  int last_opponent_score_ = 0;
  MatchPhase last_phase_ = MatchPhase::PreMatch;
  e_GameMode last_restart_ = e_GameMode_Normal;
  bool last_in_play_ = false;
  bool have_event_state_ = false;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_CONTEXT_COLLECTOR_HPP
