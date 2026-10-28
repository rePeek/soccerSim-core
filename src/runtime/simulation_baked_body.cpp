#include "runtime/simulation.hpp"
#include "player/player.hpp"
#include "player/execution/player_diagnostics.hpp"
#include "player/command/player_motion_constants.hpp"
#include "player/execution/player_baked_pose_realization.hpp"
#include "football/animation/library.hpp"


void Simulation::EnableLocomotionAnimationAdaptation(bool enabled) {
  for (auto& team : teams_) {
    if (!team) continue;
    for (Player* player : team->Players())
      if (player != nullptr) player->SetLocomotionAnimationAdaptation(enabled);
  }
}

void Simulation::EnableMovementPoseBlend(bool enabled) {
  for (auto& team : teams_) {
    if (!team) continue;
    for (Player* player : team->Players())
      if (player != nullptr) player->SetMovementPoseBlend(enabled);
  }
}
void Simulation::EnableBakedBodyProduction(bool enabled) {
  if(enabled && (!ball_ || !prepared_tick_production_ || !body_physics_production_))
    throw std::logic_error("baked bodies require prepared unified body physics");
  baked_body_production_=enabled;
  baked_pose_generation_.reset(); baked_body_poses_.clear(); baked_body_starts_.clear(); baked_body_forecasts_.clear();
  baked_body_colliders_.clear();
  if(!enabled) return;
  baked_body_poses_.resize(snapshot_players_.size()); baked_body_starts_.resize(snapshot_players_.size());
  baked_body_forecasts_.resize(snapshot_players_.size());
  baked_body_colliders_.reserve(snapshot_players_.size()*(kBakedBodySegmentCount+kBakedContactRegionCount));
  const BakedPlayerBodyCollider mapping{};
  for(std::size_t slot=0;slot<snapshot_players_.size();++slot) {
    for(std::size_t segment=0;segment<kBakedBodySegmentCount;++segment) {
      const auto id=BakedBodyColliderId(std::uint32_t(slot),static_cast<BakedBodySegment>(segment));
      body_collider_owners_[id]={snapshot_players_[slot]->GetID(),mapping.parts[segment]};
      rule_contact_episodes_[id]=false;
    }
    for(std::size_t region=0;region<kBakedContactRegionCount;++region) {
      const auto id=BakedContactRegionId(std::uint32_t(slot),static_cast<BakedContactRegion>(region));
      body_collider_owners_[id]={snapshot_players_[slot]->GetID(),kBakedContactRegionParts[region]};
    }
  }
  for(auto& [id,accepted]:rule_contact_episodes_) accepted=false;
}
void Simulation::BuildBakedBodyTape(std::span<const std::optional<PlayerBakedPoseSample>> endpoints) {
  baked_body_colliders_.clear();
  for(std::size_t slot=0;slot<snapshot_players_.size();++slot) {
    if(!snapshot_players_[slot]->IsActive() || !baked_body_starts_[slot] || !endpoints[slot]) continue;
    auto start=*baked_body_starts_[slot], end=*endpoints[slot];
    // Both samples share the single authoritative Stadium Frame.
    const auto motions=BuildBakedBodyColliderMotions(start.Geometry(),end.Geometry(),std::uint32_t(slot));
    baked_body_colliders_.insert(baked_body_colliders_.end(),motions.begin(),motions.end());
    const auto regions=BuildBakedContactRegionMotions(start.ContactRegions(),end.ContactRegions(),std::uint32_t(slot));
    baked_body_colliders_.insert(baked_body_colliders_.end(),regions.begin(),regions.end());
  }
}
void Simulation::BeginBakedBodyTick() {
  if(!baked_body_production_) return;
  if(!baked_pose_generation_ || *baked_pose_generation_!=reset_sequence_) {
    for(auto& pose:baked_body_poses_) pose.reset();
    baked_pose_generation_=reset_sequence_;
  }
  // Common actor preview is a forecast, not an advance of actor/action/RNG.
  // The final tape will use the actual single preparation pass's endpoints.
  for(auto& pose:baked_body_forecasts_) pose.reset();
  for(std::size_t slot=0;slot<snapshot_players_.size();++slot) {
    auto& actor=*snapshot_players_[slot];
    if(!actor.IsActive()) { baked_body_poses_[slot].reset(); baked_body_starts_[slot].reset(); continue; }
    auto target=actor.GetBakedPoseSample();
    if(!baked_body_poses_[slot]) baked_body_poses_[slot]=WorldBakedPose(target);
    // Player pair/root projection is already committed at Tick entry. Do not
    // turn that placement correction into this Tick's inferred body velocity.
    baked_body_poses_[slot]->state=target.state;
    baked_body_starts_[slot]=baked_body_poses_[slot];
    // ANIM-2 A11.0/A11.5: the Player owns the forecast. It returns the raw
    // clip skeleton at the next frame through PreviewRawPose; the Simulation
    // only predicts the root position and realizes the body geometry.
    auto future=actor.PreviewRawPose(1);
    future.state.position+=future.state.velocity*football::sim::kTickSeconds;
    baked_body_forecasts_[slot]=RealizeBakedPose(*baked_body_starts_[slot],future,future.outgoing_special_state);
    ++BakedBodyPreviewChecks();
  }
  BuildBakedBodyTape(baked_body_forecasts_);
}
void Simulation::CompleteBakedBodyPreparation() {
  if(!baked_body_production_) return;
  for(std::size_t slot=0;slot<snapshot_players_.size();++slot) {
    auto& actor=*snapshot_players_[slot];
    if(!actor.IsActive()) { baked_body_poses_[slot].reset(); continue; }
    auto target=actor.GetBakedPoseSample();
    baked_body_poses_[slot]=RealizeBakedPose(*baked_body_starts_[slot],target,target.outgoing_special_state);
  }
  BuildBakedBodyTape(baked_body_poses_);
}
