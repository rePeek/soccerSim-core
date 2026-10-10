// Opt-in full static+dynamic physics comparison. No production writes.
#include "sim/simulation.hpp"
#include "sim/touch_evidence_classification.hpp"
#include "football/ball/ball.hpp"
#include <type_traits>
#include "sim/player/player_body_pose_shadow.hpp"
#include "sim/player/player.hpp"
#include "sim/team/team.hpp"
#include "sim/event/touch_query.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/referee/period.hpp"
#include "foundation/time/tick_boundary.hpp"

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
  body_physics_production_colliders_ = body_physics_shadow_colliders_;
  tick.unified = compute(body_physics_shadow_colliders_);
  if (!tick.unified.contacts.empty()) {
    const auto owner = body_collider_owners_.find(tick.unified.contacts.front().collider);
    if (owner != body_collider_owners_.end()) {
      tick.player = owner->second.first;
      tick.part = owner->second.second;
    }
  }
  body_physics_shadow_candidates_.clear();
  for (const auto& prediction : body_shadow_predictions_) {
    Player* player = FindPlayerById(prediction.player);
    const auto& action = prediction.action_state;
    const auto now = GetTimelineTick();
    std::uint32_t blocks = 0;
    if (now <= last_body_ball_collision_tick_ + football::sim::TickSpan{15}) blocks |= BodyCooldown;
    if (player->GetLastTouchBias(200, now) > .01f) blocks |= BodyOwnRecentTouch;
    if (football::sim::event::TeamTouchBias(touches_, *teams_[1 - player->GetTeamID()], 200, now) <= .01f)
      blocks |= BodyNoOpponentTouch;
    const bool allowed = action.type == e_FunctionType_Movement || action.type == e_FunctionType_Trip ||
        action.type == e_FunctionType_Sliding || action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect;
    if (!allowed) blocks |= BodyActionExcluded;
    if (player->HasUniquePossession()) blocks |= BodyUniquePossession;
    if (!IsBallInPlay() || football::sim::rules::PeriodElapsed(
        IsHalfUnderway(), phase_, GetRegulationTime(), options_.half_duration)) blocks |= BodyPlayGate;
    if (action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect) {
      if (mental_images_.empty()) {
        blocks |= BodyHistoryUnavailable;
      } else {
        const auto history = std::chrono::milliseconds{player->GetReactionTime_ms() +
            static_cast<int>(football::sim::ToMilliseconds(action.elapsed))};
        const auto index = football::sim::observation::MentalImageSampleIndex(mental_images_.size(), history);
        if ((mental_images_[index].GetBallPrediction(football::sim::Seconds(1), now, *ball_) - ball_->Predict(1000)).GetLength() <= .5f)
          blocks |= BodyUnexpectedDirection;
      }
    }
    const auto body = BuildBodyCollider(prediction.start);
    float old_radius = .11f - .1f + (player->HasPossession() ? -.03f : .03f);
    if (action.type == e_FunctionType_Sliding || action.type == e_FunctionType_Interfere) old_radius += .1f;
    if (action.type == e_FunctionType_Deflect) old_radius += .2f;
    bool old_hit = false;
    for (const auto* volume : body.GetVolumes()) old_hit |= volume->IntersectsSphere(ball_->Predict(0), old_radius);
    if (!old_hit || (prediction.start.position + Vector3(0, 0, .8f) - ball_->Predict(0)).GetLength() >= 2.5f)
      blocks |= BodyOldGeometryMiss;
    const int latest_team = touches_.last_team != -1 ? touches_.last_team : first_team_;
    if (player == player->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
        football::sim::event::TeamTouchBias(touches_, *teams_[latest_team], 200, now) < .01f) blocks |= BodyControlledRoute;
    for (std::size_t part = 0; part < kPlayerBodyPartCount; ++part) {
      const auto& collider = prediction.colliders[part];
      const auto sweep = football::ball::SweepBall(tick.initial, collider, football::sim::kTickSeconds, ball_config_.radius());
      const float gap = BodyShadowGap(tick.initial.position, ball_config_.radius(), collider.start);
      // The production probe includes gravity/drag/grass. Never lose its
      // winning evidence just because a straight initial-velocity query misses.
      const bool kernel_winner = tick.player && tick.unified.contacts.front().collider == collider.id;
      if (!sweep && gap > 1e-6f && !kernel_winner) continue;
      BodyShadowCandidate candidate;
      candidate.collider = collider.id;
      candidate.player = prediction.player;
      candidate.part = static_cast<PlayerBodyPart>(part);
      candidate.swept = sweep.has_value();
      auto& c = candidate.classification;
      c.action = action.type;
      c.window = BodyActionTouchWindow(action);
      c.legacy_blocks = blocks;
      c.initial_penetration = gap < -1e-6f;
      c.starts_separated = gap > 1e-6f;
      c.foot_conflict = part == static_cast<std::size_t>(PlayerBodyPart::LowerBody) &&
          BodyFootAction(action.type) && c.window == BodyTouchWindow::Boundary;
      c.ball_speed = tick.initial.velocity.GetLength();
      c.player_speed = prediction.start.velocity.GetLength();
      body_physics_shadow_candidates_.push_back(candidate);
    }
  }
  // Keep the original upright comparison intact for attribution. Low-action
  // shapes are a separately labelled proposal, not a production filter.
  const bool low_pose = std::any_of(body_shadow_predictions_.begin(), body_shadow_predictions_.end(),
      [](const auto& p) { return p.action == e_FunctionType_Sliding || p.action == e_FunctionType_Trip; });
  if (low_pose) {
    body_physics_shadow_colliders_.clear();
    for (const auto& p : body_shadow_predictions_) {
      PlayerBodyColliderMotionIds ids{{p.colliders[0].id, p.colliders[1].id, p.colliders[2].id}};
      // Humanoid facing remains roster-local through mirrors. Convert only a copy.
      auto axis = p.start.bodyFacing;
      if (FindPlayerById(p.player)->GetTeamID() == second_team_) axis.Mirror();
      std::array<football::ball::ColliderMotion, kPlayerBodyPartCount> posed;
      BuildShadowPosedBodyMotions(p.start, p.end, p.action, axis, ids, posed);
      for (auto& collider : posed) {
        if (reverse) { MirrorShape(collider.start); MirrorShape(collider.end); }
        body_physics_shadow_colliders_.push_back(collider);
      }
    }
    tick.posed = compute(body_physics_shadow_colliders_);
  }
  body_physics_shadow_tick_ = std::move(tick);
}

void Simulation::CompleteBodyPhysicsShadow() {
  if (!body_physics_shadow_tick_ || !body_physics_shadow_tick_->production_observed) return;
  auto& tick = *body_physics_shadow_tick_;
  auto& report = body_physics_shadow_report_;
  ++report.ticks;
  for (const auto& p : body_shadow_predictions_) ++report.player_action_samples[static_cast<std::size_t>(p.action)];
  for (auto& candidate : body_physics_shadow_candidates_) {
    auto& c = candidate.classification;
    const auto slot = candidate.collider - kFirstDynamicBodyColliderId;
    const auto episode = body_geometry_episodes_.Observe(slot, tick.step_index, tick.generation, c.starts_separated);
    ++report.geometric_contacts;
    if (episode.continued) ++report.geometric_continued; else ++report.geometric_episodes;
    report.longest_geometric_episode = std::max(report.longest_geometric_episode, episode.length);
    c.continued_geometry = episode.continued;
    c.geometry_length = episode.length;
    if (tick.player && candidate.collider == tick.unified.contacts.front().collider) {
      c.position_corrected = tick.unified.contacts.front().position_corrected;
      c.closing_speed = std::max(0.0f, -tick.unified.contacts.front().relative_velocity.GetDotProduct(tick.unified.contacts.front().normal));
      for (const auto& other : body_physics_shadow_candidates_) c.candidate_parts += other.player == candidate.player;
      for (const auto& p : body_shadow_predictions_) {
        c.candidate_players += std::any_of(body_physics_shadow_candidates_.begin(), body_physics_shadow_candidates_.end(),
            [&](const auto& other) { return other.player == p.player; });
      }
      tick.classification = c;
    }
  }
  body_geometry_episodes_.Complete(tick.step_index, tick.generation);
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
  // P6a: separate geometric contact, physical impact and accepted rule touch,
  // and prove every mapped contact carries an owner identity.
  if (!tick.unified.contacts.empty()) {
    const auto& contact = tick.unified.contacts.front();
    const bool physical = contact.normal_impulse > 1e-6f;
    ++report.rule_touch.contact_ticks;
    const auto owner = body_collider_owners_.find(contact.collider);
    if (owner == body_collider_owners_.end()) {
      ++report.rule_touch.unmapped_contacts;
    } else {
      ++report.rule_touch.body_contacts;
      switch (football::sim::ClassifyTouchEvidence({true, physical, physical && matched})) {
        case football::sim::TouchEvidence::GeometricOverlapOnly:
          ++report.rule_touch.geometric_only; break;
        case football::sim::TouchEvidence::PhysicalImpactOnly:
          ++report.rule_touch.physical_only; break;
        case football::sim::TouchEvidence::PhysicalImpactAccepted:
          ++report.rule_touch.physical_accepted; break;
        default: break;
      }
    }
  }
  for (std::size_t i = body_shadow_touch_start_; i < recorded_touches_.size(); ++i) {
    const auto& touch = recorded_touches_[i];
    if (touch.type == e_TouchType_Accidental && !(effective_body_impact && tick.player &&
        touch.player == *tick.player)) ++report.rule_touch.accepted_without_impact;
  }
  if (effective_body_impact && tick.classification) {
    auto& c = *tick.classification;
    const auto slot = tick.unified.contacts.front().collider - kFirstDynamicBodyColliderId;
    const auto episode = body_impact_episodes_.Observe(slot, tick.step_index, tick.generation, c.starts_separated);
    c.continued_impact = episode.continued;
    if (episode.continued) ++report.impact_continued; else ++report.impact_episodes;
    for (std::size_t i = body_shadow_touch_start_; i < recorded_touches_.size(); ++i) {
      const auto& touch = recorded_touches_[i];
      c.intentional_touches += touch.player == *tick.player &&
          (touch.type == e_TouchType_Intentional_Kicked || touch.type == e_TouchType_Intentional_Nonkicked);
    }
    report.action_groups[static_cast<std::size_t>(c.action)].Record(c, matched);
  }
  body_impact_episodes_.Complete(tick.step_index, tick.generation);
  const auto& posed = tick.posed ? *tick.posed : tick.unified;
  const bool posed_impact = !posed.contacts.empty() && posed.contacts.front().normal_impulse > 1e-6f &&
      body_collider_owners_.contains(posed.contacts.front().collider);
  report.posed_body_impacts += posed_impact;
  if (tick.posed) {
    ++report.posed_intervals;
    const auto first_id = [](const auto& r) { return r.contacts.empty() ? 0 : r.contacts.front().collider; };
    report.pose_changed_first += first_id(posed) != first_id(tick.unified);
    report.upright_impacts_removed += effective_body_impact && !posed_impact;
    report.posed_impacts_added += !effective_body_impact && posed_impact;
  }
  // Only the last completed interval is retained, bounded independently of match length.
  body_physics_shadow_latest_ = std::move(body_physics_shadow_tick_);
  body_physics_shadow_tick_.reset();
}

void Simulation::EnableBodyPhysicsProduction(bool enabled) {
  body_physics_production_ = enabled;
  if (!enabled) return;
  if (!ball_) throw std::logic_error("simulation has no match");
  // The production path needs the same predicted collider list the P4b/P4d-1
  // shadow already builds. Both are read-only diagnostics, so this does not add
  // a second Ball step or a second actor Process.
  active_candidate_capture_ = true;
  tick_active_candidates_.reserve(snapshot_players_.size());
  EnsureActiveTouchObserver();
  DiscardBodyCollisionShadow();
  body_shadow_enabled_ = true;
  body_physics_shadow_colliders_.reserve(snapshot_players_.size() * kPlayerBodyPartCount);
  const auto slots = snapshot_players_.size() * kPlayerBodyPartCount;
  body_physics_shadow_candidates_.reserve(slots);
  body_geometry_episodes_.Resize(slots);
  body_impact_episodes_.Resize(slots);
  body_physics_shadow_ball_ = std::make_unique<football::ball::Ball>(ball_config_, pitch_);
}
