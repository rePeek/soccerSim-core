#include "sim/event/event_recognizer.hpp"

#include <algorithm>
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
  pass_motion_.reset();
  shot_motion_.reset();
  finished_motion_.clear();
  trajectories_.clear();
}

void EventRecognizer::ResolvePass(EventStatus status,
                                  std::optional<model::PlayerId> receiver, Tick now,
                                  std::optional<std::uint64_t> snapshot_step) {
  if (!pending_pass_) return;
  pending_pass_->status = status;
  pending_pass_->ended_at = now;
  pending_pass_->receiver = receiver;
  transitions_.push_back(PassEnded{pending_pass_->id, status, pending_pass_->passer,
      pending_pass_->team, receiver.value_or(model::kInvalidPlayerId), now});
  FinishTrajectory(pass_motion_, snapshot_step);
  pending_pass_.reset();
}

void EventRecognizer::ResolveShot(EventStatus status, bool goal, Tick now,
                                  std::optional<std::uint64_t> snapshot_step) {
  if (!pending_shot_) return;
  pending_shot_->status = status;
  pending_shot_->ended_at = now;
  pending_shot_->goal = goal;
  transitions_.push_back(ShotEnded{pending_shot_->id, status, pending_shot_->shooter,
      pending_shot_->team, goal, now});
  FinishTrajectory(shot_motion_, snapshot_step);
  pending_shot_.reset();
}

void EventRecognizer::CancelActive(Tick now, std::optional<std::uint64_t> snapshot_step) {
  ResolvePass(EventStatus::Cancelled, std::nullopt, now, snapshot_step);
  ResolveShot(EventStatus::Cancelled, false, now, snapshot_step);
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
      ResolvePass(EventStatus::Completed, touch->player, fact.tick, view.snapshot_step);
    } else {
      ResolvePass(EventStatus::Failed, std::nullopt, fact.tick, view.snapshot_step);
    }
  }
  if (pending_shot_) {
    ResolveShot(EventStatus::Failed, false, fact.tick, view.snapshot_step);
  }

  if (IsPassAction(touch->action_type)) {
    PassEvent event;
    event.id = NextId();
    event.passer = touch->player;
    event.team = touch->team;
    event.started_at = fact.tick;
    pending_pass_ = event;
    if (view.snapshot_step)
      pass_motion_ = MotionWindow{event.id, *view.snapshot_step, *view.snapshot_step, fact.generation,
                                  TrajectoryKind::Pass, event.passer, event.team};
    transitions_.push_back(PassStarted{event.id, event.passer, event.team, fact.tick});
  } else if (IsShotAction(touch->action_type)) {
    ShotEvent event;
    event.id = NextId();
    event.shooter = touch->player;
    event.team = touch->team;
    event.started_at = fact.tick;
    pending_shot_ = event;
    if (view.snapshot_step)
      shot_motion_ = MotionWindow{event.id, *view.snapshot_step, *view.snapshot_step, fact.generation,
                                  TrajectoryKind::Shot, event.shooter, event.team};
    transitions_.push_back(ShotStarted{event.id, event.shooter, event.team, fact.tick});
  }
}

void EventRecognizer::Advance(Tick now, const EventView& view) {
  transitions_.clear();
  if (!view.in_play || view.in_set_piece) {
    CancelActive(now, view.snapshot_step);
    return;
  }
  if (pending_pass_ && now - pending_pass_->started_at > kPassFlightTimeout) {
    ResolvePass(EventStatus::Failed, std::nullopt, now, view.snapshot_step);
  }
  if (pending_shot_ && now - pending_shot_->started_at > kShotTimeout) {
    ResolveShot(EventStatus::Failed, false, now, view.snapshot_step);
  }
}

void EventRecognizer::OnGoalConfirmed(Tick now, model::TeamSide team,
                                      std::optional<std::uint64_t> snapshot_step) {
  transitions_.clear();
  if (pending_shot_ && pending_shot_->team == team) {
    ResolveShot(EventStatus::Completed, true, now, snapshot_step);
  } else if (pending_shot_) {
    ResolveShot(EventStatus::Failed, false, now, snapshot_step);
  }
  if (pending_pass_) {
    ResolvePass(EventStatus::Completed, pending_pass_->receiver, now, snapshot_step);
  }
}

void EventRecognizer::FinishTrajectory(std::optional<MotionWindow>& pending,
                                       std::optional<std::uint64_t> last_step) {
  if (pending && last_step && *last_step >= pending->first_step) {
    pending->last_step = *last_step;
    finished_motion_.push_back(*pending);
  }
  pending.reset();
}

void EventRecognizer::PublishTrajectories(const observation::SnapshotHistory& history) {
  trajectories_.clear();
  for (const auto& window : finished_motion_) {
    EventTrajectory summary;
    summary.event = window.id;
    summary.kind = window.kind;
    summary.actor = window.actor;
    summary.team = window.team;
    summary.generation = window.generation;
    summary.first_step = window.first_step;
    summary.last_step = window.last_step;
    const observation::SnapshotRecord* previous = nullptr;
    bool continuous = true;
    history.ForEachStep(window.first_step, window.last_step, [&](const auto& record) {
      if (record.stamp.generation != window.generation) {
        continuous = false;
        previous = nullptr;
        return;
      }
      const auto& ball = record.snapshot.ball;
      if (!summary.first_ball_position) {
        summary.first_ball_position = ball.position;
        continuous &= record.stamp.step_index == window.first_step;
      }
      if (previous) {
        const bool adjacent = record.stamp.step_index - previous->stamp.step_index == 1 &&
            record.stamp.timeline_tick.value - previous->stamp.timeline_tick.value <= 1;
        if (adjacent)
          summary.ball_path_length += (ball.position - previous->snapshot.ball.position).GetLength();
        else continuous = false;
      }
      summary.last_ball_position = ball.position;
      summary.peak_ball_speed = std::max(summary.peak_ball_speed, ball.velocity.GetLength());
      ++summary.sample_count;
      previous = &record;
    });
    summary.complete = continuous && previous && previous->stamp.step_index == window.last_step;
    trajectories_.push_back(summary);
  }
  finished_motion_.clear();
}

}  // namespace football::sim::event
