#ifndef FOOTBALL_AI_COACH_RUNTIME_HPP
#define FOOTBALL_AI_COACH_RUNTIME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "command_executor.hpp"
#include "football/ai/coach_command.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// Owns the coach-side command queue and its replay log. It never owns the
// boards: it holds a pointer to the single authoritative AIEngine boards and
// mutates them only through the transactional executor.
//
// Applying happens once per simulation tick, at the top of AIEngine::Update,
// before the Player policy reads the boards. Commit time is the tick, not the
// tick the coach started thinking.
class CoachRuntime {
 public:
  static constexpr std::size_t kQueueCapacity = 64;

  explicit CoachRuntime(std::array<TacticalBoard, 2>* boards) : boards_(boards) {}

  // Queues a command for the next tick. Rejects a duplicate id (already queued
  // or already applied) and a full queue.
  bool Submit(const CoachCommandEnvelope& envelope);

  // Drains the queue: applies every pending command that is valid for `tick`.
  // Invalid commands are logged with their error and change nothing.
  void ApplyAtTick(std::uint64_t tick, const WorldState& world);

  const std::vector<CoachCommandRecord>& log() const { return log_; }
  void ClearLog() { log_.clear(); }
  std::size_t PendingCount() const { return queue_.size(); }

 private:
  std::array<TacticalBoard, 2>* boards_;
  std::vector<CoachCommandEnvelope> queue_;
  std::vector<CoachCommandRecord> log_;
  std::array<std::unordered_set<std::uint64_t>, 2> seen_ids_;
  CoachCommandExecutor executor_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_RUNTIME_HPP
