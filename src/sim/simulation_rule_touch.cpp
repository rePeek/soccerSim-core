#include "sim/simulation.hpp"
#include "sim/player/player.hpp"
#include "sim/team/team.hpp"
#include "sim/player/player_body_pose_shadow.hpp"
#include <algorithm>
namespace {
football::ball::ColliderId BodyEpisodeKey(football::ball::ColliderId id) {
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
    std::span<const football::ball::ColliderMotion> bodies) {
  // Policy is explicit: only an authorized, mapped, active player's genuine
  // approaching impact not already accepted in the geometric contact episode is
  // an accidental touch. Static hits, pure corrections and continuation do not
  // fabricate a touch. This is experimental until the pose models are accepted.
  for (const auto& contact : result.contacts) {
    if (!IsBallInPlay() || contact.normal_impulse <= 0 ||
        contact.relative_velocity.GetDotProduct(contact.normal) >= 0) continue;
    const auto owner = body_collider_owners_.find(contact.collider);
    if (owner == body_collider_owners_.end()) continue;
    auto* player = FindPlayerById(owner->second.first);
    const auto episode_key=BodyEpisodeKey(contact.collider);
    if (!player || !player->IsActive() || rule_contact_episodes_.at(episode_key)) continue;
    football::sim::event::AcceptedTouch accepted{
        GetTimelineTick(), player->GetID(), player->GetTeam()->GetTeamSide(),
        e_TouchType_Accidental, result.state.position, result.state.velocity,
        static_cast<int>(player->GetSimulationActionState().type), true};
    PublishRuleTouch({accepted, owner->second.second,
        football::sim::event::RuleTouchSource::BodyCCD, 0, 0, contact});
    rule_contact_episodes_.at(episode_key) = true;
  }
  // Re-arm only after geometric separation, not after a timer or a zero impulse.
  // Collider tapes are in pitch physics coordinates; result is in first-roster
  // coordinates. No identity remapping occurs when ends/processing order change.
  auto center = result.state.position;
  if (options_.reverse_team_processing) center.Mirror();
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
