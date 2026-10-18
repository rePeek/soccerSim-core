#include "coach_runtime.hpp"

#include <algorithm>

#include "shout_resolver.hpp"

namespace football::ai {
namespace {
CoachCommandResult RejectShout(CoachErrorCode code, const char* message) {
  CoachCommandResult result;
  result.accepted = false;
  result.error = code;
  result.message = message;
  return result;
}

const WorldPlayerState* FindWorldPlayer(const WorldState& world,
                                        football::player::PlayerId id) {
  for (const WorldPlayerState& player : world.players)
    if (player.id == id) return &player;
  return nullptr;
}
}  // namespace

bool CoachRuntime::Submit(const CoachCommandEnvelope& envelope) {
  const unsigned index = static_cast<unsigned>(CoachCommandSide(envelope.command));
  if (seen_ids_[index].count(envelope.id)) return false;
  for (const CoachCommandEnvelope& queued : queue_)
    if (queued.id == envelope.id) return false;
  if (queue_.size() >= kQueueCapacity) return false;
  queue_.push_back(envelope);
  return true;
}

CoachCommandResult CoachRuntime::ApplyShout(const IssueShout& shout,
                                            std::uint64_t tick,
                                            const WorldState& world) {
  if (world.phase == MatchPhase::Finished)
    return RejectShout(CoachErrorCode::InvalidPhase, "match is finished");
  if (shout.targets.empty())
    return RejectShout(CoachErrorCode::NoShoutTargets, "shout has no targets");

  // Validate every target before mutating any state (atomic transaction).
  for (football::player::PlayerId target : shout.targets) {
    const WorldPlayerState* player = FindWorldPlayer(world, target);
    if (player == nullptr || !player->active)
      return RejectShout(CoachErrorCode::UnknownPlayer, "shout target not observed");
    if (player->side != shout.side)
      return RejectShout(CoachErrorCode::WrongTeam, "shout target is not on this side");
    const auto last = last_shout_tick_.find(target);
    if (shout_cooldown_ticks_ != 0 && last != last_shout_tick_.end() &&
        tick - last->second < shout_cooldown_ticks_)
      return RejectShout(CoachErrorCode::ShoutCooldown, "shout target is on cooldown");
  }

  const unsigned index = static_cast<unsigned>(shout.side);
  std::vector<ActiveShout>& shouts = active_shouts_[index];
  for (football::player::PlayerId target : shout.targets) {
    // A repeated shout of the same type refreshes; other types stack up to the
    // per-player cap, oldest first.
    shouts.erase(std::remove_if(shouts.begin(), shouts.end(),
                                [&](const ActiveShout& active) {
                                  return active.target == target &&
                                         active.type == shout.type;
                                }),
                 shouts.end());
    ActiveShout active;
    active.side = shout.side;
    active.target = target;
    active.type = shout.type;
    active.issued_tick = tick;
    active.expires_at_tick = tick + ShoutResolver::DurationTicks(shout.type);
    active.response_strength = 1.0f;
    shouts.push_back(active);

    std::size_t count = 0;
    for (const ActiveShout& present : shouts)
      if (present.target == target) ++count;
    while (count > kMaxActiveShoutsPerPlayer) {
      auto oldest = shouts.end();
      for (auto it = shouts.begin(); it != shouts.end(); ++it) {
        if (it->target != target) continue;
        if (oldest == shouts.end() || it->issued_tick < oldest->issued_tick)
          oldest = it;
      }
      if (oldest == shouts.end()) break;
      shouts.erase(oldest);
      --count;
    }
    last_shout_tick_[target] = tick;
  }

  CoachCommandResult result;
  result.accepted = true;
  result.error = CoachErrorCode::Ok;
  result.message = "ok";
  result.applied_tick = tick;
  result.new_board_revision =
      (*boards_)[index].revision;  // unchanged: a shout is not a board edit
  return result;
}

void CoachRuntime::PruneExpiredShouts() {
  for (std::vector<ActiveShout>& shouts : active_shouts_)
    shouts.erase(std::remove_if(shouts.begin(), shouts.end(),
                                [&](const ActiveShout& shout) {
                                  return shout.expires_at_tick <= current_tick_;
                                }),
                 shouts.end());
}

void CoachRuntime::ApplyAtTick(std::uint64_t tick, const WorldState& world) {
  current_tick_ = tick;
  PruneExpiredShouts();

  std::vector<CoachCommandEnvelope> pending;
  pending.swap(queue_);
  // Envelopes produced by one coach turn share one observed tick and one base
  // revision; they are a single logical transaction. Rebase each same-turn
  // follow-up onto the revision its siblings already produced, while a genuinely
  // stale command (a different turn) still fails the revision check.
  std::array<std::uint64_t, 2> batch_observed{};
  std::array<bool, 2> have_batch{false, false};
  for (CoachCommandEnvelope& envelope : pending) {
    const auto side = CoachCommandSide(envelope.command);
    const unsigned index = static_cast<unsigned>(side);
    TacticalBoard& board = (*boards_)[index];
    if (have_batch[index] && envelope.observed_tick == batch_observed[index]) {
      envelope.expected_board_revision = board.revision;
    }
    have_batch[index] = true;
    batch_observed[index] = envelope.observed_tick;
    const std::uint64_t before_revision = board.revision;

    CoachCommandResult result;
    if (seen_ids_[index].count(envelope.id)) {
      result.accepted = false;
      result.error = CoachErrorCode::DuplicateId;
      result.message = "duplicate command id";
    } else if (const auto* shout = std::get_if<IssueShout>(&envelope.command)) {
      result = ApplyShout(*shout, tick, world);
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

PlayerMentalEffect CoachRuntime::MentalEffect(
    football::sim::TeamSide side, football::player::PlayerId player) const {
  PlayerMentalEffect total;
  for (const ActiveShout& shout : active_shouts_[static_cast<unsigned>(side)]) {
    if (shout.target != player) continue;
    const PlayerMentalEffect effect = ShoutResolver::EffectFor(shout.type);
    float remaining = 1.0f;
    if (shout.expires_at_tick > shout.issued_tick) {
      const std::uint64_t left =
          shout.expires_at_tick > current_tick_ ? shout.expires_at_tick - current_tick_ : 0;
      remaining = static_cast<float>(left) /
                  static_cast<float>(shout.expires_at_tick - shout.issued_tick);
    }
    const float scale = shout.response_strength * remaining;
    total.motivation += effect.motivation * scale;
    total.confidence += effect.confidence * scale;
    total.focus += effect.focus * scale;
    total.composure += effect.composure * scale;
  }
  return total;
}

}  // namespace football::ai
