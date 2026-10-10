// Opt-in full static+dynamic physics comparison. No production writes.
#include "sim/simulation.hpp"
#include "football/ball/ball.hpp"
#include <type_traits>

namespace {
void MirrorShape(football::ball::ColliderShape& shape) {
  std::visit([](auto& s) {
    using T = std::decay_t<decltype(s)>;
    if constexpr (std::is_same_v<T, football::ball::Sphere>) {
      s.center.Mirror();
    } else if constexpr (std::is_same_v<T, football::ball::Capsule>) {
      s.a.Mirror();
      s.b.Mirror();
    } else {
      s.point.Mirror();
      s.normal.Mirror();
    }
  }, shape);
}
void MirrorResult(football::ball::BallStepResult& result) {
  result.state.position.Mirror();
  result.state.velocity.Mirror();
  result.state.angular_velocity.Mirror();
  const blunted::Quaternion half_turn(0, 0, 1, 0);
  result.state.orientation = (half_turn * result.state.orientation).GetNormalized();
  for (auto& contact : result.contacts) {
    contact.point.Mirror();
    contact.normal.Mirror();
    contact.relative_velocity.Mirror();
  }
}
}

void Simulation::BeginBodyPhysicsShadow() {
  if (!body_physics_shadow_ball_) return;
  BodyPhysicsShadowTick tick;
  tick.step_index = snapshot_step_.value_or(0);
  tick.generation = reset_sequence_;
  tick.initial = ball_->state();
  // Match the existing Ball Step's pitch frame, not team-local coordinates.
  // Static ids stay attached to actual pitch shapes, even under reverse order.
  const bool reverse = options_.reverse_team_processing;
  body_physics_shadow_colliders_.assign(body_shadow_colliders_.begin(), body_shadow_colliders_.end());
  if (reverse) {
    for (auto& collider : body_physics_shadow_colliders_) {
      MirrorShape(collider.start);
      MirrorShape(collider.end);
    }
  }
  const auto compute = [&](std::span<const football::ball::ColliderMotion> bodies) {
    body_physics_shadow_ball_->Reset(tick.initial);
    if (reverse) body_physics_shadow_ball_->Mirror();
    auto result = body_physics_shadow_ball_->Step(football::ball::BallTickInput{bodies, GetBallEnvironment()});
    if (reverse) MirrorResult(result);
    return result;
  };
  tick.static_only = compute({});
  tick.unified = compute(body_physics_shadow_colliders_);
  if (!tick.unified.contacts.empty()) {
    const auto owner = body_collider_owners_.find(tick.unified.contacts.front().collider);
    if (owner != body_collider_owners_.end()) {
      tick.player = owner->second.first;
      tick.part = owner->second.second;
    }
  }
  body_physics_shadow_tick_ = std::move(tick);
}

void Simulation::CompleteBodyPhysicsShadow() {
  if (!body_physics_shadow_tick_ || !body_physics_shadow_tick_->production_observed) return;
  auto& tick = *body_physics_shadow_tick_;
  auto& report = body_physics_shadow_report_;
  ++report.ticks;
  const auto& combined = tick.unified.state;
  const auto& static_only = tick.static_only.state;
  report.position_delta.Record((combined.position - static_only.position).GetLength());
  report.velocity_delta.Record((combined.velocity - static_only.velocity).GetLength());
  report.spin_delta.Record((combined.angular_velocity - static_only.angular_velocity).GetLength());
  report.production_position_delta.Record((combined.position - tick.production.position).GetLength());
  report.production_velocity_delta.Record((combined.velocity - tick.production.velocity).GetLength());
  report.production_spin_delta.Record((combined.angular_velocity - tick.production.angular_velocity).GetLength());
  bool effective_body_impact = false;
  if (!tick.unified.contacts.empty()) {
    const auto& contact = tick.unified.contacts.front();
    if (tick.player) {
      ++report.dynamic_first;
      effective_body_impact = contact.normal_impulse > 1e-6f;
      if (effective_body_impact) ++report.effective_body_impacts;
    } else {
      ++report.static_first;
      if (body_shadow_contact_) ++report.dynamic_query_superseded;
    }
    if (contact.normal_impulse == 0) ++report.zero_impulse_contacts;
  }
  bool matched = false;
  for (std::size_t i = body_shadow_touch_start_; i < recorded_touches_.size(); ++i) {
    const auto& touch = recorded_touches_[i];
    if (touch.type != e_TouchType_Accidental) continue;
    if (effective_body_impact && touch.player == *tick.player) {
      ++report.matched_touches;
      matched = true;
    } else {
      ++report.missed_touches;
    }
  }
  if (effective_body_impact && !matched) ++report.unmatched_impacts;
  // Only the last completed interval is retained, bounded independently of match length.
  body_physics_shadow_latest_ = std::move(body_physics_shadow_tick_);
  body_physics_shadow_tick_.reset();
}
