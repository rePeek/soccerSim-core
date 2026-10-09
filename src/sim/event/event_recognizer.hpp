#ifndef FOOTBALL_SIM_EVENT_EVENT_RECOGNIZER_HPP
#define FOOTBALL_SIM_EVENT_EVENT_RECOGNIZER_HPP

#include <optional>
#include <vector>

#include "sim/event/event.hpp"
#include "sim/event/event_transition.hpp"
#include "sim/fact/simulation_fact.hpp"

namespace football::sim::event {

// Read-only facts for one recognition step. Kept small on purpose: the
// recognizer classifies accepted actions and resolves their lifecycle; it never
// re-implements rules or physics.
struct EventView {
  bool in_play = false;
  bool in_set_piece = false;
};

// Recognizes cross-tick football behaviors from the immutable fact stream. It
// owns only its pending-event state; finished behavior is published as
// transitions for the referee/event log. It draws no RNG and reads no ball or
// actor state, so recognition cannot perturb physics or the match.
class EventRecognizer {
 public:
  void Consume(const StampedFact& fact, const EventView& view);
  void Advance(Tick now, const EventView& view);
  void OnGoalConfirmed(Tick now, model::TeamSide team);
  void Reset();

  const std::vector<EventTransition>& transitions() const { return transitions_; }
  bool HasActivePass() const { return pending_pass_.has_value(); }
  bool HasActiveShot() const { return pending_shot_.has_value(); }

 private:
  EventId NextId() { return next_id_++; }
  void ResolvePass(EventStatus status, std::optional<model::PlayerId> receiver, Tick now);
  void ResolveShot(EventStatus status, bool goal, Tick now);
  void CancelActive(Tick now);

  EventId next_id_ = 1;
  std::optional<PassEvent> pending_pass_;
  std::optional<ShotEvent> pending_shot_;
  std::vector<EventTransition> transitions_;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_EVENT_RECOGNIZER_HPP
