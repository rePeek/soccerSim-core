#include "coach_runtime.hpp"

namespace football::ai {

bool CoachRuntime::Submit(const CoachCommandEnvelope& envelope) {
  const unsigned index = static_cast<unsigned>(CoachCommandSide(envelope.command));
  if (seen_ids_[index].count(envelope.id)) return false;
  for (const CoachCommandEnvelope& queued : queue_)
    if (queued.id == envelope.id) return false;
  if (queue_.size() >= kQueueCapacity) return false;
  queue_.push_back(envelope);
  return true;
}

void CoachRuntime::ApplyAtTick(std::uint64_t tick, const WorldState& world) {
  std::vector<CoachCommandEnvelope> pending;
  pending.swap(queue_);
  for (const CoachCommandEnvelope& envelope : pending) {
    const auto side = CoachCommandSide(envelope.command);
    const unsigned index = static_cast<unsigned>(side);
    TacticalBoard& board = (*boards_)[index];
    const std::uint64_t before_revision = board.revision;

    CoachCommandResult result;
    if (seen_ids_[index].count(envelope.id)) {
      result.accepted = false;
      result.error = CoachErrorCode::DuplicateId;
      result.message = "duplicate command id";
    } else {
      result = executor_.Apply(board, envelope, world, tick);
    }
    seen_ids_[index].insert(envelope.id);

    CoachCommandRecord record;
    record.call_id = envelope.id;
    record.observed_tick = envelope.observed_tick;
    record.applied_tick = result.accepted ? tick : 0;
    record.side = side;
    record.before_revision = before_revision;
    record.after_revision = board.revision;
    record.command = envelope.command;
    record.result = result;
    log_.push_back(record);
  }
}

}  // namespace football::ai
