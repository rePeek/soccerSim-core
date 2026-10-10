#ifndef FOOTBALL_SIM_EVENT_EVENT_RECOGNIZER_HPP
#define FOOTBALL_SIM_EVENT_EVENT_RECOGNIZER_HPP

#include <optional>
#include <vector>

#include "sim/event/event.hpp"
#include "sim/event/event_transition.hpp"
#include "sim/event/accepted_touch.hpp"
#include "sim/event/event_trajectory.hpp"
#include "sim/observation/snapshot_history.hpp"

namespace football::sim::event {

// Read-only facts for one recognition step. Kept small on purpose: the
// recognizer classifies accepted actions and resolves their lifecycle; it never
// re-implements rules or physics.
struct EventView {
  bool in_play = false;
  bool in_set_piece = false;
  // Present only during an executing Simulation step, not diagnostic facts.
  std::optional<std::uint64_t> snapshot_step;
  // Reset generation at the accepted-touch instant, for trajectory windows.
  std::uint64_t generation = 0;
};

// Recognizes cross-tick football behaviors from the immutable fact stream. It
// owns only its pending-event state; finished behavior is published as
// transitions for the referee/event log. It draws no RNG and reads no ball or
// actor state, so recognition cannot perturb physics or the match.
class EventRecognizer {
 public:
  void Consume(const AcceptedTouch& touch, const EventView& view);
  void Advance(Tick now, const EventView& view);
  void OnGoalConfirmed(Tick now, model::TeamSide team,
                       std::optional<std::uint64_t> snapshot_step = std::nullopt);
  void Reset();

  const std::vector<EventTransition>& transitions() const { return transitions_; }
  bool HasActivePass() const { return pending_pass_.has_value(); }
  bool HasActiveShot() const { return pending_shot_.has_value(); }

  // Call after the whole step has committed its Snapshot, never inside a fact
  // callback. Analyses use only retained history and do not change transitions.
  void PublishTrajectories(const observation::SnapshotHistory& history);
  const std::vector<EventTrajectory>& trajectories() const { return trajectories_; }

 private:
  EventId NextId() { return next_id_++; }
  void ResolvePass(EventStatus status, std::optional<model::PlayerId> receiver, Tick now,
                   std::optional<std::uint64_t> snapshot_step);
  void ResolveShot(EventStatus status, bool goal, Tick now,
                   std::optional<std::uint64_t> snapshot_step);
  void CancelActive(Tick now, std::optional<std::uint64_t> snapshot_step);
  struct MotionWindow {
    EventId id;
    std::uint64_t first_step, last_step, generation;
    TrajectoryKind kind;
    model::PlayerId actor;
    model::TeamSide team;
  };
  void FinishTrajectory(std::optional<MotionWindow>& pending,
                        std::optional<std::uint64_t> last_step);

  EventId next_id_ = 1;
  std::optional<PassEvent> pending_pass_;
  std::optional<ShotEvent> pending_shot_;
  std::vector<EventTransition> transitions_;
  std::optional<MotionWindow> pass_motion_, shot_motion_;
  std::vector<MotionWindow> finished_motion_;
  std::vector<EventTrajectory> trajectories_;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_EVENT_RECOGNIZER_HPP
