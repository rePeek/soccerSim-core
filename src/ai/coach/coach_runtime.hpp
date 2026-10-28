#ifndef FOOTBALL_AI_COACH_RUNTIME_HPP
#define FOOTBALL_AI_COACH_RUNTIME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "command_executor.hpp"
#include "football/ai/coach_command.hpp"
#include "football/ai/shout.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// Owns the coach-side command queue, its replay log and the transient shout
// state. It never owns the boards: it holds a pointer to the single
// authoritative AIEngine boards and mutates them only through the
// transactional executor. Shouts never touch the boards.
//
// Applying happens once per simulation tick, at the top of AIEngine::Update,
// before the board is read by any intent consumer. Commit time is the tick,
// not the tick the coach started thinking.
class CoachRuntime {
 public:
  static constexpr std::size_t kQueueCapacity = 64;
  static constexpr std::uint64_t kDefaultShoutCooldownTicks = 3000;
  static constexpr std::size_t kMaxActiveShoutsPerPlayer = 2;

  explicit CoachRuntime(std::array<TacticalBoard, 2>* boards) : boards_(boards) {}

  // Queues a command for the next tick. Rejects a duplicate id (already queued
  // or already applied) and a full queue.
  bool Submit(const CoachCommandEnvelope& envelope);

  // Drains the queue: applies every pending command that is valid for `tick`.
  // Invalid commands are logged with their error and change nothing. Also
  // prunes expired shouts.
  void ApplyAtTick(std::uint64_t tick, const WorldState& world);

  const std::vector<CoachCommandRecord>& log() const { return log_; }
  void ClearLog() { log_.clear(); }
  std::size_t PendingCount() const { return queue_.size(); }

  void SetShoutCooldownTicks(std::uint64_t ticks) {
    shout_cooldown_ticks_ = ticks;
  }
  const std::vector<ActiveShout>& ActiveShouts(
      football::sim::TeamSide side) const {
    return active_shouts_[static_cast<unsigned>(side)];
  }
  // Summed, remaining-fraction-scaled effect of the active shouts on a player.
  PlayerMentalEffect MentalEffect(football::sim::TeamSide side,
                                  football::player::PlayerId player) const;
  std::uint64_t CurrentTick() const { return current_tick_; }

 private:
  CoachCommandResult ApplyShout(const IssueShout& shout, std::uint64_t tick,
                                const WorldState& world);
  void PruneExpiredShouts();

  std::array<TacticalBoard, 2>* boards_;
  std::vector<CoachCommandEnvelope> queue_;
  std::vector<CoachCommandRecord> log_;
  std::array<std::unordered_set<std::uint64_t>, 2> seen_ids_;
  CoachCommandExecutor executor_;

  std::array<std::vector<ActiveShout>, 2> active_shouts_;
  std::unordered_map<football::player::PlayerId, std::uint64_t> last_shout_tick_;
  std::uint64_t shout_cooldown_ticks_ = kDefaultShoutCooldownTicks;
  std::uint64_t current_tick_ = 0;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_RUNTIME_HPP
