// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_PLAYER
#define _HPP_PLAYER

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>

#include "football/animation/playback.hpp"
#include "football/contact/player_ground_collider.hpp"
#include "football/contact/player_touch_type.hpp"
#include "football/foundation/time/tick_boundary.hpp"
#include "football/match/team_side.hpp"
#include "football/pitch/pitch.hpp"
#include "football/player/execution/player_motion_profile.hpp"
#include "football/player/execution/player_team_match_facts.hpp"
#include "football/player/formation_entry.hpp"
#include "football/player/player_action.hpp"
#include "football/player/player_action_request.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_control.hpp"
#include "football/player/player_definition.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/player_action_feedback.hpp"
#include "player/execution/player_baked_pose_sample.hpp"
#include "player/execution/player_execution_port.hpp"
#include "player/execution/player_locomotion_animation.hpp"
#include "player/execution/player_selected_action.hpp"
#include "player/execution/player_tick_context.hpp"
#include "player/execution/player_touch_choice.hpp"
#include "player/execution/player_team_view.hpp"
#include "player/player_observation.hpp"

namespace football::sim { class PlayerRuntimeSink; class PlayerSendOffSink; }
namespace football::sim::player { class PlayerTouchSink; }
class AnimationLibrary;
struct PlayerCommandInputs;

class Player;
class PlayerExecution;

// Facts Runtime publishes to each Player. This is a per-tick projection of its
// team's match state; Player never stores or owns a Team object.
struct PlayerTeamFacts {
  // The end this team defends in the Stadium Frame: -1 (-X) or +1 (+X).
  int defending_direction = -1;
  float possession_amount = 1.0f;
  float fading_possession_amount = 1.0f;
  Player* designated_possession_player = nullptr;

  // The direction this team attacks in the Stadium Frame.
  int attack_direction() const { return -defending_direction; }
};

// PEX-4: the Player is the player entity - identity, capability, match state,
// the authoritative kinematic state and the lifecycle. The execution process
// (control/request consumption, scheduling, action selection, animation
// playback and the legacy compatibility projection) lives in PlayerExecution,
// composed below. It is an implementation detail: Runtime keeps driving the
// Player through this interface and never names the executor.
//
// Authority map:
//   kinematic_state_ / ground_collider_  committed motion (the only writer)
//   execution_->ActionState()        the only action timeline
//   execution_->Playback()           pose sampling/blending
//   everything else here             player entity and match state
class Player final : private PlayerExecutionPort {

 public:
  Player(int team_id, football::sim::TeamSide side,
         football::player::PlayerDefinition model,
         const FormationEntry& formation_entry,
         std::uint8_t schedule_phase,
         const AnimationLibrary& animations, const football::pitch::Pitch& pitch);
  ~Player();
  // Explicit teardown reset before deletion; no hidden clock destructor path.
  void Exit(football::sim::Tick now);

  football::player::PlayerId GetID() const override { return model_.id; }
  const football::player::PlayerDefinition& GetModel() const override { return model_; }
  // Static body geometry authority: derived once from the definition, not
  // re-derived per tick or per consumer (BODY-1.5).
  const football::player::PlayerBodyProfile& BodyProfile() const override { return body_profile_; }

  // LOCO-2.3b: value snapshot of the committed locomotion tick, captured by
  // CommitLocomotion so the animation adapter never re-runs the motion model.
  const std::optional<LocomotionAnimationInput>& LocomotionAnimationInputSnapshot() const;
  bool LocomotionAnimationAdaptation() const;
  void SetLocomotionAnimationAdaptation(bool enabled);
  // ANIM-1 A5: pose-blend gate, owned by the execution's playback state.
  void SetMovementPoseBlend(bool enabled);
  bool MovementPoseBlend() const;

  bool IsActive() const override { return is_active_; }

  // get ready for some action
  void Activate();
  // go back to bench/take a shower
  void Deactivate(const football::ball::Ball& ball, football::sim::Tick now);

  void ResetPosition(const Vector3 &newPos, const Vector3 &focusPos);
  void OffsetPosition(const Vector3 &offset);

  // Gameplay-facing movement state. It is the single public reader: position,
  // velocity and orientation all come from `kinematic_state_`, the committed
  // authoritative state. The legacy animation/spatial projection lives in the
  // executor and is never a second authority.
  // CheckSimulationKinematicOracle() enforces the projection bit-exactly.
  // Actors must be positioned through Player::ResetPosition / OffsetPosition so
  // that this state cannot be left stale.
  Vector3 GetPosition() const override { return kinematic_state_.position; }
  Vector3 GetDirectionVec() const override { return kinematic_state_.GetFacing(); }
  inline Vector3 GetBodyDirectionVec() const { return kinematic_state_.bodyFacing; }
  inline Vector3 GetMovement() const { return kinematic_state_.velocity; }
  // Gameplay reads continuous relative orientation. The quantized
  // relBodyAngle remains animation-selection compatibility only.
  radian GetRelBodyAngle() const;
  e_Velocity GetEnumVelocity() const;
  float GetFloatVelocity() const;
  inline e_FunctionType GetCurrentFunctionType() const {
    return GetSimulationActionState().type;
  }
  e_FunctionType GetPreviousFunctionType() const;
  // H3e authority boundary: true only when this actor may leave the legacy
  // animation root-motion path and use the simulation-owned procedural
  // movement model instead. See PlayerActionState::IsPureLocomotion().
  bool IsEligibleForProceduralLocomotion(bool retaining_ball) const;

  const PlayerKinematicState &GetKinematicState() const override { return kinematic_state_; }
  const PlayerGroundCollider &GetGroundCollider() const { return ground_collider_; }
  // Gameplay reads the sole persistent, simulation-authoritative action
  // schedule. The compatibility projection is measurement-only.
  const PlayerActionState &GetSimulationActionState() const;

  // P2a: the Player is the only writer of the authoritative kinematic state.
  // Execution computes the locomotion proposal; this entry commits it and
  // keeps the ground collider and the animation feed in step. `tick_start` is
  // the tick-start state, so a later action selection in the same tick cannot
  // change what this tick's locomotion was.
  PlayerKinematicState CommitLocomotion(const PlayerCommand &command,
                                        const PlayerKinematicState &tick_start,
                                        bool restart_needs_simulation, float dt) override;

  // ANIM-1.1 / ANIM-2 A11.5/A11.7: per-player action request / reaction
  // contract. The control model is explicit:
  //   SetControl() -> continuous movement intent (PlayerMotion authority).
  //   Play()       -> discrete actions (pass/shoot/tackle/...).
  //   React()      -> passive physics reactions (trips/bumps).
  // `Play(ControlAction::None)` is REJECTED: continuous movement must go
  // through SetControl, otherwise a Movement clip would be selected without
  // changing the authoritative locomotion intent.
  //
  // Both arbitrate immediately and record the outcome. An accepted request is
  // retained as the Player's pending input and consumed exactly once by the
  // next action tick; an accepted Trip reaction stages atomically (see React).
  RequestOutcome Play(const PlayerActionRequest& request);
  // `ball_retainer` only gates the passive-trip animation guard; null means
  // "no retainer" (the contact caller passes the real one). A second trip that
  // loses the guard does not overwrite the first accepted reaction, and a
  // non-executable reaction never clobbers a staged trip.
  RequestOutcome React(const PlayerReaction& reaction,
                       const Player* ball_retainer = nullptr);
  const std::optional<PlayerActionRequest>& PendingActionRequest() const;
  const std::optional<PlayerReaction>& PendingReaction() const;
  RequestOutcome LastActionRequestOutcome() const;
  RequestOutcome LastReactionOutcome() const;
  int ActionRequestAttempts() const;
  int ActionRequestAccepts() const;
  int ActionRequestDefers() const;
  int ActionRequestRejects() const;
  // Accepted by arbitration but the command build produced no executable
  // discrete action (Rejected/Fallback/Unsupported). The request is consumed
  // but not counted as executed.
  int ActionRequestBuildFailures() const;
  // ANIM-2 A11.5/A11.7: what was actually CONSUMED by the action tick, not
  // what was installed. Consuming a request builds a non-empty command queue;
  // scheduling a trip interrupt does not guarantee the trip clip installs.
  int ActionRequestInputsConsumed() const;
  int ReactionTripInterruptsScheduled() const;
  // Accepted active intents discarded because a passive Trip preempted them.
  int ActionRequestPreemptions() const;

  // ANIM-1 A3: the execution owns the animation playback state and computes the
  // final pose; the Animation module stays stateless.
  int AuthorityChecks() const;
  int AuthorityMatches() const;
  int AuthorityMismatches() const;
  const football::sim::TouchChoice &TouchSelection() const;
  int TouchSelectionSuccesses() const;
  const PoseFrame& PlaybackPose() const;
  float PlaybackBaseAngle() const;
  const football::animation::PlaybackState& PlaybackState() const;
  // ANIM-1.1: the single final pose for this player (default when inactive).
  PoseFrame Pose() const;
  const football::sim::SelectedAction& InstalledAction() const;
  // Last discrete-action execution result: source Command, matched type,
  // install/contact/completion and whether a new action interrupted it.
  const PlayerActionFeedback& ActionFeedback() const;
  int InstallChecks() const;
  int InstallMatches() const;
  int InstallMismatches() const;
  int InstallCursorChecks() const;
  int InstallCursorMatches() const;
  int InstallCursorMismatches() const;
  // Validates the authoritative schedule against the compatibility projection.
  // It is intentionally a fatal invariant in all builds.
  void CheckSimulationActionOracle() const;

  int GetReactionTime_ms() const override;
  float GetControlSpeed() const;
  void SetControl(const PlayerControl& control);
  void ClearControl();

  // facing, derived speed and the collider center must all match the
  // compatibility spatial projection exactly. An epsilon cannot mask a
  // synchronization bug.
  bool IsSpatialProjectionConsistent() const;
  // Fatal (all build types) form of the same invariant, with the diverging
  // field reported. Callers are the state writers and state restore, so the
  // contract is checked at every movement mutation point.
  void CheckSimulationKinematicOracle() const;

  float GetDecayingPositionOffsetLength() const;
  const Vector3 &GetDecayingPositionOffset() const;

  // Tick-local borrows; no history owner or persistent world port.
  void Process(const football::sim::PlayerTickContext& tick,
               const football::sim::player::PlayerObservation& observation,
               football::sim::player::PlayerTouchSink& touch_sink,
               football::sim::PlayerRuntimeSink& runtime_sink);
  // Advances motion/animation once and returns distance for post-touch fatigue.
  float PrepareTick(const football::sim::PlayerTickContext& tick,
                    const football::sim::player::PlayerObservation& observation,
                    football::sim::player::PlayerTouchSink& touch_sink,
                    football::sim::PlayerRuntimeSink& runtime_sink);
  void CommitTick(const football::sim::PlayerTickContext& tick, float distance);

  // The ability in force: the definition's base ability scaled by this
  // player's current fatigue, and by nothing else. There is no per-team or
  // per-actor difficulty modifier.
  float GetStat(football::player::PlayerStat name) const;
  float GetVelocityMultiplier() const;
  float GetMaxVelocity() const;
  // S1.1: the motion capability actually in force, in ONE place. The static
  // part comes from the definition (MakePlayerMotionProfile); it is not baked
  // into the definition or a MotionClip. It is the only source both execution
  // and prediction may read, so they cannot drift apart.
  //
  // Deliberate decision, pinned by a test: fatigue scales the *abilities*
  // (GetStat) but NOT the motion profile. In particular max_speed keeps the
  // established GetMaxVelocity() formula with no fatigue factor. Whether
  // fatigue should slow a player down is a separate behaviour change, not part
  // of unifying the parameter source.
  PlayerMotionProfile EffectiveMotionProfile() const;

  // P19.3: the single read-only clip identity for runtime consumers.
  AnimationId GetCurrentAnimationId() const;
  PlayerBakedPoseSample GetBakedPoseSample() const;
  // ANIM-2 A11.0/A11.5: read-only raw-clip body forecast. It returns the raw
  // clip skeleton at the requested frame without advancing the action clock,
  // drawing RNG or mutating the Player. The name is deliberate: this is the
  // RAW clip pose, not the final blended playback pose, so a forecast and the
  // actual endpoint can legitimately differ mid-transition.
  // The requested frame is clamped to the clip range in both directions.
  PlayerBakedPoseSample PreviewRawPose(int frame_offset) const;

  void SetLastTouchTick(football::sim::Tick tick) { last_touch_tick_ = tick; }
  football::sim::Tick GetLastTouchTick() const override { return last_touch_tick_; }
  void SetLastTouchType(e_TouchType touchType) { this->lastTouchType = touchType; }
  e_TouchType GetLastTouchType() const override { return lastTouchType; }
  // The ability-dependent decay is a continuous estimate, not a grid deadline.
  float GetLastTouchBias(int decay_ms, football::sim::Tick at) const;

  float GetFatigueFactorInv() const { return fatigueFactorInv; }
  void RelaxFatigue(float howMuch) {
    fatigueFactorInv += howMuch;
    fatigueFactorInv = clamp(fatigueFactorInv, 0.01f, 1.0f);
  }

  void ResetSituation(const Vector3 &focusPos, football::sim::Tick now);

  int GetTeamID() const override { return team_id_; }
  football::sim::TeamSide GetTeamSide() const override { return side_; }
  // The end this team defends in the Stadium Frame: -1 (-X) or +1 (+X).
  int GetDefendingDirection() const { return team_facts_.defending_direction; }
  // The direction this team attacks in the Stadium Frame.
  int GetAttackDirection() const { return team_facts_.attack_direction(); }
  float GetTeamFadingPossessionAmount() const {
    return team_facts_.fading_possession_amount;
  }
  Player* GetTeamDesignatedPossessionPlayer() const {
    return team_facts_.designated_possession_player;
  }
  // Runtime publishes the per-tick match facts; Player never holds Team.
  void SetTeamContext(const PlayerTeamFacts& facts) { team_facts_ = facts; }
  Vector3 GetPitchPosition();
  bool TouchPending() const;
  bool TouchAnim() const;
  Vector3 GetTouchPos() const;
  int GetTouchFrame() const;
  int GetCurrentFrame() const;
  int GetFrameNum() const;
  int GetFrameCount() const;
  void SelectRetainAnim();
  FormationEntry GetFormationEntry() const override { return formationEntry_; }
  void SetFormationEntry(FormationEntry entry) { formationEntry_ = entry; }
  void SetDynamicFormationEntry(FormationEntry entry) { dynamicFormationEntry = entry; }
  FormationEntry GetDynamicFormationEntry() { return dynamicFormationEntry; }
  bool HasPossession() const override;
  bool HasBestPossession() const;
  bool HasUniquePossession() const;
  // Non-negative by contract: the stored value is an unsigned millisecond
  // estimate, so a saturated/internal value must never read back negative.
  int GetTimeNeededToGetToBall_ms() const {
    return static_cast<int>(std::min<unsigned int>(timeNeededToGetToBall_ms, 1000000u));
  }
  int GetTimeNeededToGetToBall_optimistic_ms() const {
    return static_cast<int>(std::min<unsigned int>(timeNeededToGetToBall_optimistic_ms, 1000000u));
  }
  int GetTimeNeededToGetToBall_previous_ms() const {
    return static_cast<int>(std::min<unsigned int>(timeNeededToGetToBall_previous_ms, 1000000u));
  }
  void SetDesiredTimeToBall_ms(int ms) { desiredTimeToBall_ms = ms; }
  int GetDesiredTimeToBall_ms() const { return clamp(desiredTimeToBall_ms, timeNeededToGetToBall_ms, 1000000.0f); }
  bool AllowLastDitch(bool includingPossessionAmount = true) const;
  void TriggerControlledBallCollision() { triggerControlledBallCollision = true; }
  bool IsControlledBallCollisionTriggered() const { return triggerControlledBallCollision; }
  void ResetControlledBallCollisionTrigger() { triggerControlledBallCollision = false; }
  void ResetTouchRecord() override {
    last_touch_tick_ = {};
    lastTouchType = e_TouchType_None;
  }
  void UpdatePossessionStats(football::ball::Ball& ball, const football::player::PlayerTeamMatchFacts& opponent, football::sim::Tick now, const Player* retainer, const Player* designated_own);
  float GetClosestOpponentDistance(const PlayerTeamView& opponent) const;
  void GiveYellowCard(football::sim::Tick effective_tick) { cards++; card_effective_tick_ = effective_tick; }
  void GiveRedCard(football::sim::Tick effective_tick) { cards += 3; card_effective_tick_ = effective_tick; }
  bool HasCards() { return cards > 0; }
  // Legacy disciplinary units: yellow +1, direct red +3; >1 means send-off.
  int CardCount() const { return cards; }
  football::sim::Tick CardEffectiveTick() const { return card_effective_tick_; }
  void SendOff(const football::ball::Ball& ball, football::sim::Tick now, football::sim::PlayerSendOffSink* send_off);
  float GetStaminaStat() const;

 private:
  // The port calls the player already implements publicly above (GetID,
  // GetModel, BodyProfile, IsActive, GetPosition, GetDirectionVec,
  // GetKinematicState, HasPossession, GetFormationEntry, GetLastTouchTick,
  // GetReactionTime_ms, GetTeamID, GetTeamSide, CommitLocomotion) are their own
  // overrides: one declaration, one owner. Only the private commits and the
  // pointer-keyed identity live here.
  void CommitKinematics(const PlayerKinematicState& next) override;
  void CommitLegacyActionMotion() override;
  void CheckKinematicOracle() const override { CheckSimulationKinematicOracle(); }
  void ApplyPositionCorrection(const Vector3 &offset) override;
  Player& Actor() override { return *this; }
  const Player& Actor() const override { return *this; }

  // Binds the port for one execution call; the executor releases it on return.
  PlayerExecutionPort& Port() const;

  // Immutable actor-owned initialization data, never an external borrow.
  const football::player::PlayerDefinition model_;
  const football::player::PlayerBodyProfile body_profile_;
  // Non-unique 0..9 offset for staggered 100 ms work, never identity or order.
  const std::uint8_t schedule_phase_;

  // Own team identity plus the match facts Runtime publishes. Player knows
  // which team it belongs to but never owns or stores a Team object.
  const int team_id_;
  const football::sim::TeamSide side_;
  FormationEntry formationEntry_;
  PlayerTeamFacts team_facts_;

  PlayerKinematicState kinematic_state_;
  PlayerGroundCollider ground_collider_;

  // The execution process: the action timeline, playback, pending inputs,
  // scheduling and the legacy compatibility projection, in one owned copy.
  std::unique_ptr<PlayerExecution> execution_;

  bool is_active_ = false;

  football::sim::Tick last_touch_tick_{};
  e_TouchType lastTouchType;

  float fatigueFactorInv = 0.0f;

  FormationEntry dynamicFormationEntry;
  bool hasPossession = false;
  bool hasBestPossession = false;
  bool hasUniquePossession = false;
  unsigned int timeNeededToGetToBall_ms = 1000;
  unsigned int timeNeededToGetToBall_optimistic_ms = 1000;
  unsigned int timeNeededToGetToBall_previous_ms = 1000;
  bool triggerControlledBallCollision = false;
  int desiredTimeToBall_ms = 0;
  int cards = 0; // 1: yellow; 2: second yellow; >=3: red
  football::sim::Tick card_effective_tick_{};
};

#endif
