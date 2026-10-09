#include "sim/event/event_recognizer.hpp"

#include <variant>

#include "sim/animation/types.hpp"

namespace football::sim::event {
namespace {

// Engineering flight windows, not calibrated distributions. A behavior that is
// not resolved inside its window ends as Failed rather than staying Active.
constexpr auto kPassFlightTimeout = football::sim::Seconds(4);
constexpr auto kShotTimeout = football::sim::Seconds(3);

bool IsPassAction(int action) {
  return action == static_cast<int>(e_FunctionType_ShortPass) ||
         action == static_cast<int>(e_FunctionType_LongPass) ||
         action == static_cast<int>(e_FunctionType_HighPass);
}

bool IsShotAction(int action) {
  return action == static_cast<int>(e_FunctionType_Shot);
}

}  // namespace

void EventRecognizer::Reset() {
  next_id_ = 1;
  pending_pass_.reset();
  pending_shot_.reset();
  transitions_.clear();
}

void EventRecognizer::ResolvePass(EventStatus status,
                                  std::optional<model::PlayerId> receiver, Tick now) {
  if (!pending_pass_) return;
  pending_pass_->status = status;
  pending_pass_->ended_at = now;
  pending_pass_->receiver = receiver;
  transitions_.push_back(PassEnded{pending_pass_->id, status, pending_pass_->passer,
      pending_pass_->team, receiver.value_or(model::kInvalidPlayerId), now});
  pending_pass_.reset();
}

void EventRecognizer::ResolveShot(EventStatus status, bool goal, Tick now) {
  if (!pending_shot_) return;
  pending_shot_->status = status;
  pending_shot_->ended_at = now;
  pending_shot_->goal = goal;
  transitions_.push_back(ShotEnded{pending_shot_->id, status, pending_shot_->shooter,
      pending_shot_->team, goal, now});
  pending_shot_.reset();
}

void EventRecognizer::CancelActive(Tick now) {
  ResolvePass(EventStatus::Cancelled, std::nullopt, now);
  ResolveShot(EventStatus::Cancelled, false, now);
}

void EventRecognizer::Consume(const StampedFact& fact, const EventView& view) {
  transitions_.clear();
  const auto* touch = std::get_if<BallTouchFact>(&fact.fact);
  if (touch == nullptr) return;
  if (!view.in_play || view.in_set_piece) return;

  // Resolve an open behavior with this touch before opening a new one, so a
  // teammate's reception completes the pass and can start its own action.
  if (pending_pass_) {
    if (touch->team == pending_pass_->team) {
      ResolvePass(EventStatus::Completed, touch->player, fact.tick);
    } else {
      ResolvePass(EventStatus::Failed, std::nullopt, fact.tick);
    }
  }
  if (pending_shot_) {
    ResolveShot(EventStatus::Failed, false, fact.tick);
  }

  if (IsPassAction(touch->action_type)) {
    PassEvent event;
    event.id = NextId();
    event.passer = touch->player;
    event.team = touch->team;
    event.started_at = fact.tick;
    pending_pass_ = event;
    transitions_.push_back(PassStarted{event.id, event.passer, event.team, fact.tick});
  } else if (IsShotAction(touch->action_type)) {
    ShotEvent event;
    event.id = NextId();
    event.shooter = touch->player;
    event.team = touch->team;
    event.started_at = fact.tick;
    pending_shot_ = event;
    transitions_.push_back(ShotStarted{event.id, event.shooter, event.team, fact.tick});
  }
}

void EventRecognizer::Advance(Tick now, const EventView& view) {
  transitions_.clear();
  if (!view.in_play || view.in_set_piece) {
    CancelActive(now);
    return;
  }
  if (pending_pass_ && now - pending_pass_->started_at > kPassFlightTimeout) {
    ResolvePass(EventStatus::Failed, std::nullopt, now);
  }
  if (pending_shot_ && now - pending_shot_->started_at > kShotTimeout) {
    ResolveShot(EventStatus::Failed, false, now);
  }
}

void EventRecognizer::OnGoalConfirmed(Tick now, model::TeamSide team) {
  transitions_.clear();
  if (pending_shot_ && pending_shot_->team == team) {
    ResolveShot(EventStatus::Completed, true, now);
  } else if (pending_shot_) {
    ResolveShot(EventStatus::Failed, false, now);
  }
  if (pending_pass_) {
    ResolvePass(EventStatus::Completed, pending_pass_->receiver, now);
  }
}

}  // namespace football::sim::event
