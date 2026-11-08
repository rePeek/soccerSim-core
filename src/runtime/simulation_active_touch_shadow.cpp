#include <algorithm>
#include "runtime/simulation.hpp"
#include "player/execution/diagnostics/player_active_touch_shadow.hpp"

class Simulation::ActiveTouchEvents final : public ActiveTouchShadowSink {
 public:
  explicit ActiveTouchEvents(Simulation& owner) : owner_(owner) {}
  void Observe(const ActiveTouchObservation& source) override {
    auto observation = source;
    const auto& passive = owner_.body_physics_shadow_tick_;
    if (passive && passive->step_index == owner_.snapshot_step_.value_or(0) &&
        passive->generation == owner_.reset_sequence_) {
      observation.passive_endpoint = passive->unified.state;
      const auto impact = std::find_if(passive->unified.contacts.begin(), passive->unified.contacts.end(),
          [](const auto& c) { return c.normal_impulse > 0; });
      if (impact != passive->unified.contacts.end()) {
        const auto owner = owner_.body_collider_owners_.find(impact->collider);
        if (owner != owner_.body_collider_owners_.end()) {
          observation.same_part_passive_impact = owner->second.first == observation.candidate.player &&
              owner->second.second == observation.candidate.body_part;
          observation.other_player_passive_impact = owner->second.first != observation.candidate.player;
        }
      }
    }
    // Legacy actor observations are diagnostic only. Prepared production
    // collects and arbitrates touch proposals in RunPreparedPlayerTick.
    if (owner_.active_touch_report_enabled_) {
      owner_.active_touch_shadow_report_.Record(std::move(observation),
          owner_.snapshot_step_.value_or(0), owner_.reset_sequence_, owner_.ball_config_,
          owner_.body_physics_shadow_ball_.get());
    }
  }
 private:
  Simulation& owner_;
};
void Simulation::EnsureActiveTouchObserver() {
  if (!ball_) throw std::logic_error("simulation has no match");
  if (active_touch_shadow_sink_) return;
  active_touch_shadow_sink_ = std::make_unique<ActiveTouchEvents>(*this);
}

void Simulation::EnableActiveTouchShadow(bool enabled) {
  active_touch_report_enabled_ = enabled;
  active_touch_shadow_report_ = {};
  if (!enabled) {
    active_touch_shadow_sink_.reset();
    return;
  }
  EnsureActiveTouchObserver();
  active_touch_shadow_report_.latest.reserve(snapshot_players_.size() * 4);
}
