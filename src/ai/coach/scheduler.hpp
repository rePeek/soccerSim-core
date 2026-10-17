#ifndef FOOTBALL_AI_COACH_SCHEDULER_HPP
#define FOOTBALL_AI_COACH_SCHEDULER_HPP

#include <cstdint>

#include "football/ai/coach_context.hpp"

namespace football::ai {

// When a coach turn should run. Periodic maintenance plus event triggers, with
// debounce, an event cooldown and a hard per-match call budget. All cadences
// are configuration, never hard-coded into the simulation loop.
struct CoachSchedulerConfig {
  std::uint64_t periodic_ticks = 30000;      // 5 minutes of match time
  std::uint64_t debounce_ticks = 18000;      // 3 minutes minimum between turns
  std::uint64_t event_cooldown_ticks = 5000; // 50 s between event-driven turns
  int max_calls = 8;
};

class CoachScheduler {
 public:
  explicit CoachScheduler(CoachSchedulerConfig config = {}) : config_(config) {}

  // Pure decision; does not mutate. Call OnTurnRun when a turn actually runs.
  bool ShouldRun(const CoachContext& context, bool event_triggered) const;
  void OnTurnRun(std::uint64_t tick, bool event_triggered = false);
  int CallsUsed() const { return calls_used_; }
  int RemainingBudget() const { return config_.max_calls - calls_used_; }
  void Reset();

 private:
  CoachSchedulerConfig config_;
  int calls_used_ = 0;
  bool have_last_turn_ = false;
  std::uint64_t last_turn_tick_ = 0;
  bool have_last_event_turn_ = false;
  std::uint64_t last_event_turn_tick_ = 0;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_SCHEDULER_HPP
