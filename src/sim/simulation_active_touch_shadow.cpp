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
    owner_.active_touch_shadow_report_.Record(std::move(observation),
        owner_.snapshot_step_.value_or(0), owner_.reset_sequence_, owner_.ball_config_,
        owner_.body_physics_shadow_ball_.get());
  }
 private:
  Simulation& owner_;
};
void Simulation::EnableActiveTouchShadow(bool enabled) {
  active_touch_shadow_sink_.reset();
  active_touch_shadow_report_ = {};
  if (!enabled) return;
  if (!ball_) throw std::logic_error("simulation has no match");
  active_touch_shadow_report_.latest.reserve(snapshot_players_.size() * 4);
  active_touch_shadow_sink_ = std::make_unique<ActiveTouchEvents>(*this);
}
