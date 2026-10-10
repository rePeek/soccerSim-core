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
      if (passive->player && !passive->unified.contacts.empty() &&
          passive->unified.contacts.front().normal_impulse > 0) {
        observation.same_part_passive_impact = *passive->player == observation.candidate.player &&
            passive->part == observation.candidate.body_part;
        observation.other_player_passive_impact = *passive->player != observation.candidate.player;
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
  if (!enabled) {
    // The production candidate observer survives: the report is optional.
    return;
  }
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
  if (tick_active_candidates_.size() < tick_active_candidates_.capacity())
    tick_active_candidates_.push_back(*candidate);
}

void Simulation::ArbitratePendingActiveTouches() {
  if (tick_active_candidates_.empty()) return;
  auto& arb = active_arbitration_;
  ++arb.ticks_with_candidates;
  arb.candidates += tick_active_candidates_.size();
  if (tick_active_candidates_.size() > 1) ++arb.multi_candidate_ticks;
  const auto winner = ArbitrateActiveImpulse(tick_active_candidates_);
  bool passive_impact = false, passive_same_part = false, owner_known = false;
  if (body_physics_shadow_tick_ && !body_physics_shadow_tick_->unified.contacts.empty() &&
      body_physics_shadow_tick_->unified.contacts.front().normal_impulse > 0) {
    passive_impact = true;
    if (body_physics_shadow_tick_->player) {
      owner_known = true;
      passive_same_part = winner && *body_physics_shadow_tick_->player == winner->player &&
          *body_physics_shadow_tick_->part == winner->body_part;
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
  active_impulse_production_ = enabled;
  if (!enabled) return;
  if (!ball_) throw std::logic_error("simulation has no match");
  active_candidate_capture_ = true;
  tick_active_candidates_.reserve(snapshot_players_.size());
  EnsureActiveTouchObserver();
}
