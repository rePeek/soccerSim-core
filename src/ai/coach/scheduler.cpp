#include "scheduler.hpp"

namespace football::ai {

bool CoachScheduler::ShouldRun(const CoachContext& context,
                               bool event_triggered) const {
  if (calls_used_ >= config_.max_calls) return false;
  const std::uint64_t tick = context.observed_tick;
  if (have_last_turn_ && tick - last_turn_tick_ < config_.debounce_ticks)
    return false;
  if (event_triggered) {
    if (!have_last_event_turn_ ||
        tick - last_event_turn_tick_ >= config_.event_cooldown_ticks)
      return true;
  }
  if (!have_last_turn_ || tick - last_turn_tick_ >= config_.periodic_ticks)
    return true;
  return false;
}

void CoachScheduler::OnTurnRun(std::uint64_t tick, bool event_triggered) {
  ++calls_used_;
  last_turn_tick_ = tick;
  have_last_turn_ = true;
  if (event_triggered) {
    last_event_turn_tick_ = tick;
    have_last_event_turn_ = true;
  }
}

void CoachScheduler::Reset() {
  calls_used_ = 0;
  have_last_turn_ = false;
  last_turn_tick_ = 0;
  have_last_event_turn_ = false;
  last_event_turn_tick_ = 0;
}

}  // namespace football::ai
