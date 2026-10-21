#include "runtime/simulation.hpp"
#include "player/player.hpp"
#include "player/player_body_pose_shadow.hpp"
#include "referee/referee.hpp"
#include "runtime/touch_evidence_classification.hpp"
#include <algorithm>
namespace {
football::geometry::ColliderId BodyEpisodeKey(football::geometry::ColliderId id) {
  if(id>=kFirstBakedContactRegionId && id<kFirstDynamicBodyColliderId) {
    const auto offset=id-kFirstBakedContactRegionId;
    return PlayerBodyColliderId(offset/kBakedContactRegionCount,kBakedContactRegionParts[offset%kBakedContactRegionCount]);
  }
  if(id<kFirstBakedBodyColliderId) return id;
  const auto offset=id-kFirstBakedBodyColliderId;
  const auto segment=offset%kBakedBodySegmentCount;
  const auto part=segment==0 ? PlayerBodyPart::UpperBody :
      segment==1 ? PlayerBodyPart::Head : PlayerBodyPart::LowerBody;
  return PlayerBodyColliderId(offset/kBakedBodySegmentCount,part);
}
}

void Simulation::PublishRuleTouch(football::sim::event::RuleTouch touch) {
  // A RuleTouch contains reviewed provenance, not an inferred Snapshot delta.
  touch.accepted.ball_position = ball_->state().position;
  touch.accepted.ball_velocity = ball_->state().velocity;
  touch.step = snapshot_step_.value_or(0);
  touch.generation = reset_sequence_;
  rule_touches_.push_back(touch);
  touch_sink_->OnAcceptedTouch(touch.accepted);
}

void Simulation::PublishBodyRuleTouches(const football::ball::BallStepResult& result,
    std::span<const football::geometry::ColliderMotion> bodies) {
  // Only mapped active players' approaching impacts can become rule touches.
  // Boot impact during an authorized kick is reviewed deliberate play; other
  // live impacts are deflections. Ready restart release additionally requires
  // the designated taker. Corrections/continuation never fabricate a touch.
  // Classification remains experimental pending complete P6c acceptance.
  for (const auto& contact : result.contacts) {
    if (contact.normal_impulse <= 0 ||
        contact.relative_velocity.GetDotProduct(contact.normal) >= 0) continue;
    const auto owner = body_collider_owners_.find(contact.collider);
    if (owner == body_collider_owners_.end()) continue;
    auto* player = FindPlayerById(owner->second.first);
    const auto episode_key=BodyEpisodeKey(contact.collider);
    if (!player || !player->IsActive() || rule_contact_episodes_.at(episode_key)) continue;
    const auto action=player->GetSimulationActionState().type;
    const bool boot=contact.collider>=kFirstBakedContactRegionId && contact.collider<kFirstDynamicBodyColliderId &&
        ((contact.collider-kFirstBakedContactRegionId)%kBakedContactRegionCount==1 ||
         (contact.collider-kFirstBakedContactRegionId)%kBakedContactRegionCount==2);
    const bool kick_action=action==e_FunctionType_ShortPass || action==e_FunctionType_LongPass ||
        action==e_FunctionType_HighPass || action==e_FunctionType_Shot;
    // Swing-speed gate: the boot must be actively swinging relative to the
    // player's root translation. A planted support foot moves with the root
    // (near-zero swing) and is never promoted to a deliberate strike just
    // because the action is a Shot/Pass.
    // (swing gate below is the pure IsDeliberateKickSwing predicate)
    const bool kick=boot && play_authorized_ && kick_action &&
        football::sim::IsDeliberateKickSwing(contact.surface_velocity,
                                            player->GetKinematicState().velocity);
    if(!IsBallInPlay()) {
      const auto& restart=restarts_.context();
      if(!kick || !set_piece_active_ || restart.taker!=player || !restart.state ||
          restart.state->phase!=football::sim::RestartPhase::Ready) continue;
    }
    football::sim::event::AcceptedTouch accepted{
        GetTimelineTick(), player->GetID(), player->GetTeamSide(),
        kick ? e_TouchType_Intentional_Kicked : e_TouchType_Accidental, result.state.position, result.state.velocity,
        static_cast<int>(action), !kick, kick};
    PublishRuleTouch({accepted, owner->second.second,
        football::sim::event::RuleTouchSource::BodyCCD, 0, 0, contact});
    rule_contact_episodes_.at(episode_key) = true;
  }
  // Re-arm only after geometric separation, not after a timer or a zero impulse.
  // Collider tapes are in pitch physics coordinates; result is in first-team
  // coordinates. No identity remapping occurs when ends/processing order change.
  auto center = result.state.position;
  // Four leg primitives remain physically distinct, but continuation transfers
  // across adjacent lower-body surfaces without another rule touch. Re-arm only
  // when the whole semantic group has separated, never leaf-by-leaf or by time.
  for (auto& [key, accepted] : rule_contact_episodes_) {
    if(!accepted) continue;
    accepted=std::any_of(bodies.begin(),bodies.end(),[&](const auto& collider) {
      if(BodyEpisodeKey(collider.id)!=key) return false;
      const bool reported=std::any_of(result.contacts.begin(),result.contacts.end(),
          [&](const auto& c) { return BodyEpisodeKey(c.collider)==key; });
      return reported || BodyShadowGap(center,ball_config_.radius(),collider.end)<=0;
    });
  }
}
