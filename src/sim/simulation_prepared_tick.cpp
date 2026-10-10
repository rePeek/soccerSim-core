#include "sim/simulation.hpp"
#include "sim/player/player.hpp"
#include "sim/team/team.hpp"
#include "sim/player/possession.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
void MirrorResult(football::ball::BallStepResult& result) {
  result.state.position.Mirror();
  result.state.velocity.Mirror();
  result.state.angular_velocity.Mirror();
  result.state.orientation = (blunted::Quaternion(0, 0, 1, 0) * result.state.orientation).GetNormalized();
  for (auto& c : result.contacts) {
    c.point.Mirror(); c.normal.Mirror(); c.relative_velocity.Mirror();
  }
  if (result.active_impulse) {
    result.active_impulse->impulse.Mirror();
    result.active_impulse->contact_point.Mirror();
  }
  if (result.endpoint_constraint) {
    result.endpoint_constraint->position.Mirror();
    result.endpoint_constraint->velocity.Mirror();
  }
}
bool SameState(const football::ball::BallState& a, const football::ball::BallState& b) {
  return a.position == b.position && a.velocity == b.velocity &&
         a.angular_velocity == b.angular_velocity && !(a.orientation != b.orientation);
}
// Explicit replacement timing: all proposals read the passive endpoint and the
// previous authoritative touch history. Neither the second actor nor referee
// sees an uncommitted proposal. Runtime retention is likewise deferred.
struct PreparationPorts final : football::sim::PlayerTouchPreparationSink,
                                football::sim::PlayerRuntimeSink,
                                football::sim::AcceptedTouchSink {
  bool second_roster = false;
  std::vector<football::sim::PreparedPlayerTouch> touches;
  std::optional<Player*> release;
  void PrepareTouch(const football::sim::PreparedPlayerTouch& input) override {
    auto touch = input;
    if (second_roster) {
      touch.desired_ball_center.Mirror(); touch.target_velocity.Mirror();
      touch.fact.ball_position.Mirror(); touch.fact.ball_velocity.Mirror();
    }
    touches.push_back(touch);
  }
  void SetBallRetainer(Player* value) override {
    // Acquisition is committed only if that actor's anchor wins. A release
    // belongs to the currently retained actor and is not an impulse/event.
    if (!value) release = value;
  }
  void OnAcceptedTouch(const football::sim::event::AcceptedTouch&) override {
    throw std::logic_error("accepted touch published before Ball commit");
  }
};
}

void Simulation::EnablePreparedTickProduction(bool enabled) {
  if (!ball_) throw std::logic_error("simulation has no match");
  prepared_tick_production_ = enabled;
  if (enabled && !preparation_ball_)
    preparation_ball_ = std::make_unique<football::ball::Ball>(ball_config_, pitch_);
  pending_active_impulse_.reset();
  tick_active_candidates_.clear();
  committed_ball_tick_.reset();
  rule_touches_.clear();
  rule_contact_episodes_.clear();
  for (const auto& [id, owner] : body_collider_owners_)
    rule_contact_episodes_.emplace(id, false);
}

void Simulation::RunPreparedPlayerTick() {
  using namespace football::ball;
  const bool reverse = options_.reverse_team_processing;
  const auto bodies = body_physics_production_ ?
      std::span<const ColliderMotion>{body_physics_production_colliders_} :
      std::span<const ColliderMotion>{};
  BallTickInput input{bodies, GetBallEnvironment()};
  // Predict is const and does not consume force. Only the final Step below
  // commits physical state, using the identical initial state and collider tape.
  Mirror(false, false, reverse);
  auto passive = ball_->Predict(input);
  Mirror(false, false, reverse);
  if (reverse) MirrorResult(passive);
  preparation_ball_->Reset(passive.state);
  CaptureMentalImage(preparation_ball_.get());
  PreparationPorts ports;
  ports.touches.reserve(snapshot_players_.size() * 2);
  std::vector<std::pair<Player*, float>> prepared;
  prepared.reserve(snapshot_players_.size());
  const auto prepare_team = [&](int id) {
    auto& team = *teams_[id];
    auto& opponent = *teams_[1 - id];
    football::sim::player::PrepareTeamPossession(team, opponent, play_authorized_,
        set_piece_active_, ball_retainer_, best_possession_team_);
    for (auto* actor : team.GetAllPlayers()) {
      if (!actor->IsActive()) continue;
      // Rebind the Ball reference; do not swap the authoritative owner.
      auto base = PlayerTickFacts(*actor);
      football::sim::PlayerTickContext facts{
          base.now, base.play_authorized, base.set_piece_active, base.ball_in_play,
          base.half_underway, *preparation_ball_, base.ball_environment,
          base.ball_retainer, base.designated_possession_player, base.last_touch_player,
          base.touches, base.restart, base.pitch, base.own_team, base.opponent_team,
          base.first_processing_team, base.second_processing_team, base.processing_slot,
          base.restart_needs_simulation, base.rng, nullptr, &ports};
      const auto before = preparation_ball_->state();
      const auto distance = actor->PrepareTick(facts, mental_images_, ports, ports);
      if (!SameState(before, preparation_ball_->state()))
        throw std::logic_error("actor mutated the passive endpoint preparation view");
      prepared.emplace_back(actor, distance);
      MeasureBodyShadowEndpoint(*actor);
    }
    football::sim::player::FinishTeamPossession(team, opponent);
  };
  Mirror(first_team_ == 1, first_team_ == 0, false);
  prepare_team(first_team_);
  Mirror(true, true, true);
  preparation_ball_->Mirror();
  ports.second_roster = true;
  prepare_team(second_team_);
  Mirror(first_team_ == 0, first_team_ == 1, true);
  preparation_ball_->Mirror();

  pending_active_impulse_.reset();
  tick_active_candidates_.clear();
  const football::sim::PreparedPlayerTouch* anchor = nullptr;
  // Keep the exact proposal provenance: one actor may submit multiple proposals.
  std::vector<std::pair<ActiveImpulseCandidate, const football::sim::PreparedPlayerTouch*>> provenance;
  provenance.reserve(ports.touches.size());
  for (const auto& touch : ports.touches) {
    if (touch.retain_anchor) {
      if (!anchor || touch.fact.player < anchor->fact.player) anchor = &touch;
      continue;
    }
    football::sim::player::TouchProposalInput proposal;
    proposal.passive_endpoint = passive.state;
    proposal.desired_ball_center = touch.desired_ball_center;
    proposal.target_velocity = touch.target_velocity;
    proposal.config = ball_config_;
    const auto model = football::sim::player::ProposeActiveTouch(proposal);
    // A rejected proposal is not silently executed by the legacy path.
    if (!model.reachable) continue;
    ActiveImpulseCandidate candidate;
    candidate.player = touch.fact.player;
    candidate.body_part = touch.part;
    candidate.action = static_cast<e_FunctionType>(touch.fact.action_type);
    candidate.impulse = model.impulse;
    candidate.velocity_change_magnitude = model.impulse.impulse.GetLength() / ball_config_.mass();
    tick_active_candidates_.push_back(candidate);
    provenance.emplace_back(candidate, &touch);
  }
  const bool passive_impact = std::any_of(passive.contacts.begin(), passive.contacts.end(),
      [](const auto& contact) { return contact.normal_impulse > 0; });
  auto winner = ArbitrateActiveImpulse(tick_active_candidates_);
  const auto authority = DecideContactAuthority({passive_impact, false, winner.has_value()});
  if (authority != ContactAuthority::ActiveTouch) winner.reset();
  if (anchor && !passive_impact) winner.reset();
  pending_active_impulse_ = winner;
  if (winner) {
    input.active_impulse = winner->impulse;
    if (reverse) {
      input.active_impulse->impulse.Mirror();
      input.active_impulse->contact_point.Mirror();
    }
  } else if (anchor && !passive_impact) {
    input.endpoint_constraint = BallEndpointConstraint{anchor->desired_ball_center, blunted::Vector3(0)};
    if (reverse) input.endpoint_constraint->position.Mirror();
  }
  tick_active_candidates_.clear();
  Mirror(false, false, reverse);
  auto result = ball_->Step(input); // the sole authoritative normal physical write
  Mirror(false, false, reverse);
  if (reverse) MirrorResult(result);
  committed_ball_tick_ = result;
  if (body_physics_shadow_tick_) {
    // This is the actual committed endpoint, including the accepted active input.
    body_physics_shadow_tick_->production = result.state;
    body_physics_shadow_tick_->production_observed = true;
  }

  // All consumers now see committed physical state in the common actor frame.
  Mirror(reverse, !reverse, false);
  const auto* previous_retainer = ball_retainer_;
  if (ports.release) ball_retainer_ = nullptr;
  if (anchor && input.endpoint_constraint) ball_retainer_ = FindPlayerById(anchor->fact.player);
  PublishBodyRuleTouches(result, bodies);
  if (winner) {
    const auto selected = std::find_if(provenance.begin(), provenance.end(), [&](const auto& entry) {
      const auto& c = entry.first;
      return c.player == winner->player && c.body_part == winner->body_part &&
          c.action == winner->action && c.impulse.impulse == winner->impulse.impulse &&
          c.impulse.contact_point == winner->impulse.contact_point;
    });
    if (selected == provenance.end()) throw std::logic_error("arbitration winner has no touch provenance");
    const auto* fact = selected->second;
    PublishRuleTouch({fact->fact, fact->part,
        football::sim::event::RuleTouchSource::PreparedAction, 0, 0, std::nullopt});
  } else if (anchor && input.endpoint_constraint && !ports.release) {
    // Anchoring is not a repeated new rule touch. Publish acquisition only.
    if (previous_retainer != ball_retainer_)
      PublishRuleTouch({anchor->fact, anchor->part,
          football::sim::event::RuleTouchSource::RetainAcquisition, 0, 0, std::nullopt});
  }
  football::sim::observation::RefreshLatestMentalImageBallPredictions(mental_images_, *ball_);
  Mirror(reverse, !reverse, false);
  // No animation/action execution or RNG redraw in the commit phase.
  Mirror(first_team_ == 1, first_team_ == 0, false);
  for (const auto& [actor, distance] : prepared)
    if (actor->GetTeamID() == first_team_) actor->CommitTick(PlayerTickFacts(*actor), distance);
  Mirror(true, true, true);
  for (const auto& [actor, distance] : prepared)
    if (actor->GetTeamID() == second_team_) actor->CommitTick(PlayerTickFacts(*actor), distance);
  Mirror(first_team_ == 0, first_team_ == 1, true);
}
