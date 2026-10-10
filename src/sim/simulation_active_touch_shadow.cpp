#include <algorithm>
#include "sim/simulation.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_active_touch_shadow.hpp"

namespace {
void MirrorState(football::ball::BallState& state) {
  state.position.Mirror(); state.velocity.Mirror(); state.angular_velocity.Mirror();
  state.orientation = (blunted::Quaternion(0, 0, 1, 0) * state.orientation).GetNormalized();
}
}
class Simulation::ActiveTouchEvents final : public ActiveTouchShadowSink {
 public:
  explicit ActiveTouchEvents(Simulation& owner) : owner_(owner) {}
  void Observe(const ActiveTouchObservation& source) override {
    auto observation = source;
    const Player* player = owner_.FindPlayerById(source.candidate.player);
    if (player && player->GetTeamID() == owner_.second_team_) {
      MirrorState(observation.before); MirrorState(observation.after);
      observation.raw_animation_point.Mirror(); observation.player_position.Mirror();
      observation.candidate.target_velocity.Mirror(); observation.candidate.target_angular_velocity.Mirror();
    }
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
    // P5e: the real candidate path is owned by Simulation and runs whether or not
    // the diagnostic report observes it.
    owner_.SubmitActiveTouchCandidate(observation);
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
  active_candidate_capture_ = enabled || body_physics_production_;
  if (!enabled) {
    if (!active_candidate_capture_) active_touch_shadow_sink_.reset();
    return;
  }
  tick_active_candidates_.reserve(snapshot_players_.size() * 4);
  EnsureActiveTouchObserver();
  active_touch_shadow_report_.latest.reserve(snapshot_players_.size() * 4);
}

void Simulation::SubmitActiveTouchCandidate(const ActiveTouchObservation& observation) {
  if (!active_candidate_capture_) return;
  const auto& passive = observation.passive_endpoint;
  const football::sim::player::TouchProposalInput proposal{
      passive ? *passive : observation.before,
      observation.raw_animation_point,
      observation.candidate.target_velocity,
      {},
      ball_config_,
      observation.incoming_retain_override ? 1.0f : 0.4f};
  const auto model = football::sim::player::ProposeActiveTouch(proposal);
  const auto candidate =
      MakeActiveImpulseCandidate(observation, model, ball_config_.mass());
  if (!candidate) return;
  // Never silently discard valid observations at an arbitrary capacity limit.
  tick_active_candidates_.push_back(*candidate);
}

void Simulation::ArbitratePendingActiveTouches() {
  pending_active_impulse_.reset();
  if (tick_active_candidates_.empty()) return;
  auto& arb = active_arbitration_;
  ++arb.ticks_with_candidates;
  arb.candidates += tick_active_candidates_.size();
  if (tick_active_candidates_.size() > 1) ++arb.multi_candidate_ticks;
  const auto winner = ArbitrateActiveImpulse(tick_active_candidates_);
  bool passive_impact = false, passive_same_part = false, owner_known = false;
  if (body_physics_shadow_tick_ &&
      body_physics_shadow_tick_->step_index == snapshot_step_.value_or(0) &&
      body_physics_shadow_tick_->generation == reset_sequence_) {
    const auto& contacts = body_physics_shadow_tick_->unified.contacts;
    const auto impact = std::find_if(contacts.begin(), contacts.end(),
        [](const auto& c) { return c.normal_impulse > 0; });
    passive_impact = impact != contacts.end();
    if (passive_impact) {
      const auto owner = body_collider_owners_.find(impact->collider);
      owner_known = owner != body_collider_owners_.end();
      passive_same_part = owner_known && winner && owner->second.first == winner->player &&
          owner->second.second == winner->body_part;
    }
  }
  const auto authority = DecideContactAuthority(
      {passive_impact, passive_impact && owner_known && passive_same_part, winner.has_value()});
  if (authority == ContactAuthority::PassiveImpact) {
    ++arb.passive_wins;
    if (passive_impact && owner_known && passive_same_part) ++arb.passive_same_part_wins;
    else ++arb.passive_other_player_wins;
    pending_active_impulse_.reset();
  } else if (authority == ContactAuthority::ActiveTouch && winner) {
    ++arb.active_wins;
    ++arb.winner_actions.at(static_cast<std::size_t>(winner->action));
    pending_active_impulse_ = winner;
  } else {
    pending_active_impulse_.reset();
  }
  tick_active_candidates_.clear();
}

void Simulation::EnableActiveImpulseProduction(bool enabled) {
  EnablePreparedTickProduction(enabled);
}
