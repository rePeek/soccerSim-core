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

#include "player/player.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

#include "football/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "football/ball/ball_timing.hpp"
#include "football/player/execution/player_body_facing.hpp"
#include "football/player/execution/player_motion.hpp"
#include "player/execution/player_execution.hpp"
#include "player/execution/player_locomotion.hpp"
#include "player/execution/player_motion_constants.hpp"
#include "player/execution/reachability.hpp"
#include "player/execution/roster_query.hpp"
#include "player/execution/player_send_off_sink.hpp"

using football::ball::Ball;

namespace {

// Execute every 10 ms; refresh the intercept belief every 100 ms, staggered
// by the dense match-local index. Between refreshes the estimate is retained.
constexpr football::sim::TickSpan kReachabilityRefresh{10};

// Compatibility adapter from the legacy PlayerCommand to the continuous
// locomotion model. This is execution semantics, not a Command solver: the
// legacy animation idle deadband is intentionally preserved for now. It should
// be revisited when MoveSolver starts emitting low approach speeds.
PlayerLocomotionInput BuildLegacyLocomotionInput(
    const PlayerCommand& command, const PlayerKinematicState& state,
    float maxSpeed, const Vector3& idle_facing_fallback) {
  PlayerLocomotionInput input;
  float desiredSpeed = clamp(command.desiredVelocityFloat, 0.0f, maxSpeed);
  if (FloatToEnumVelocity(desiredSpeed) == e_Velocity_Idle) {
    desiredSpeed = 0.0f;
  }
  input.desiredVelocity =
      command.desiredDirection.Get2D().GetNormalized(state.GetFacing()) *
      desiredSpeed;
  input.idleFacing =
      command.useDesiredLookAt
          ? (command.desiredLookAt - state.position)
                .Get2D()
                .GetNormalized(state.GetFacing())
          : idle_facing_fallback;
  return input;
}

// Bit comparison, not an epsilon: a projection that is only approximately
// right is a synchronization bug, and masking it would let a later procedural
// producer inherit an inconsistent baseline.
bool Vector3BitsEqual(const Vector3 &a, const Vector3 &b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

}  // namespace

Player::Player(int team_id, football::sim::TeamSide side,
               football::player::PlayerDefinition model,
               const AnimationLibrary& animations, const football::pitch::Pitch& pitch)
      : model_(std::move(model)),
      body_profile_(football::player::MakePlayerBodyProfile(model_)),
      team_id_(team_id),
      side_(side),
      execution_(std::make_unique<PlayerExecution>(animations)) {
  // `pitch` is part of the construction contract (the actor is placed in the
  // pitch frame before its first tick) but the Player keeps no borrowed
  // reference to it: geometry arrives through the tick context.
  (void)pitch;
  match_state_.last_touch_tick = {};
  match_state_.last_touch_type = e_TouchType_None;
  match_state_.fatigue_factor_inv = 1.0;
  SetDesiredTimeToBall_ms(0);
  triggerControlledBallCollision = false;
  match_state_.cards = 0;
  match_state_.card_effective_tick = {};
}

Player::~Player() = default;

PlayerExecutionPort& Player::Port() const {
  // Observation queries bind the port read-only; commits use the same
  // interface. The borrow is expressed here, in the entity, exactly once.
  return const_cast<PlayerExecutionPort&>(
      static_cast<const PlayerExecutionPort&>(*this));
}

void Player::Exit(football::sim::Tick now) {
  if (is_active_) {
    // Preserve the former base destructor's reset window without invoking
    // roster callbacks: Team::Exit may already have deleted other players.
    execution_->Exit(Port(), now);
    is_active_ = false;
  }
}

bool Player::IsSpatialProjectionConsistent() const {
  const PlayerExecution::Projection projection = execution_->CompatibilityProjection();
  return Vector3BitsEqual(kinematic_state_.position, projection.position) &&
         Vector3BitsEqual(kinematic_state_.velocity, projection.movement) &&
         Vector3BitsEqual(kinematic_state_.bodyFacing, projection.body_facing) &&
         Vector3BitsEqual(ground_collider_.center,
                          kinematic_state_.position.Get2D());
}

void Player::CheckSimulationKinematicOracle() const {
  assert(IsSpatialProjectionConsistent() &&
         "gameplay kinematic state diverged from the compatibility spatial state");
}

PlayerKinematicState Player::CommitLocomotion(
    const PlayerCommand &command, const PlayerKinematicState &tick_start,
    bool restart_needs_simulation, float dt) {
  const float max_speed = GetMaxVelocity();
  PlayerLocomotionInput input = BuildLegacyLocomotionInput(
      command, tick_start, max_speed, tick_start.bodyFacing);
  if (restart_needs_simulation) {
    // The legacy idle-speed deadband stops approach ~0.7 m from a target.
    // Pending legal positioning needs continuous low speeds; live-play semantics
    // and the shared locomotion physics primitive are otherwise unchanged.
    input.desiredVelocity =
        command.desiredDirection.Get2D().GetNormalized(tick_start.GetFacing()) *
        clamp(command.desiredVelocityFloat, 0.f, max_speed);
  }
  // S1.1: one source for the capability in force, shared with prediction, so the
  // two cannot drift apart. max_speed keeps the established GetMaxVelocity()
  // semantics, and the runtime modifiers are applied inside it (currently none
  // touch the profile -- see the declaration for the pinned decision).
  const PlayerMotionProfile profile = EffectiveMotionProfile();

  PlayerKinematicState next =
      PlayerMotion::StepContinuous(tick_start, profile, input.desiredVelocity,
                                   input.idleFacing, dt)
          .state;
  // Torso model consumes the resulting locomotion facing and writes only
  // bodyFacing; order and parameters are unchanged from the legacy path.
  PlayerBodyFacingInput bodyInput;
  bodyInput.desiredFacing = next.GetFacing();
  if (command.useDesiredLookAt) {
    bodyInput.desiredFacing =
        (command.desiredLookAt - next.position).Get2D().GetNormalized(next.GetFacing());
  }
  PlayerBodyFacing::Step(next, bodyInput, PlayerBodyFacingParameters{}, dt);
  // LOCO-2.3b: hand the snapshot the animation adapter reads later this tick to
  // the executor. `next` already reflects bodyFacing, so the adapter sees the
  // committed motion without re-running the physics model.
  execution_->RecordLocomotionAnimationInput(
      MakeLocomotionAnimationInput(tick_start, next, input.desiredVelocity, body_profile_));
  CommitKinematics(next);
  return next;
}

void Player::CommitKinematics(const PlayerKinematicState& next) {
  // Full-state commit: the caller computed every field, including `speed`; this
  // point only installs it and keeps the ground collider consistent. No oracle
  // here: the compatibility projection is refreshed by the caller's sync step,
  // and the two sides are momentarily allowed to disagree.
  kinematic_state_ = next;
  ground_collider_.SetCenter(kinematic_state_.position);
}

void Player::CommitLegacyActionMotion() {
  // S0.1: adopting the baked action motion is legitimate ONLY for the legacy
  // animation-root-motion paths (and the lifecycle resets that re-read the
  // compatibility pose). Pure locomotion never comes through here.
  // become the authority for a tick the Player already computed. The commit
  // still goes through the single write point, which also keeps the ground
  // collider in step.
  const PlayerExecution::Projection projection = execution_->CompatibilityProjection();
  PlayerKinematicState next;
  next.position = projection.position;
  next.velocity = projection.movement;
  next.bodyFacing = projection.body_facing;
  CommitKinematics(next);
  CheckSimulationKinematicOracle();
}

void Player::ApplyPositionCorrection(const Vector3 &offset) {
  // S0.2: the Player is the authority for a corrected position, wherever the
  // correction is *evaluated* (PlayerContact, or still the goalkeeper boundary
  // clamps inside the action tick). Commit it first, then let the compatibility
  // bookkeeping adopt the same offset (startPos, previous position, smuggle
  // offsets) without re-deriving the Player state.
  //
  // Only the position changes. Velocity, facing and bodyFacing stay exactly the
  // values the previous commit installed, and `speed` is deliberately not
  // recomputed: a position correction is an instantaneous displacement, not new
  // motion. That is also what the legacy path produced, since the projection
  // never changed its movement/direction for an offset.
  PlayerKinematicState corrected = kinematic_state_;
  corrected.position += offset;
  CommitKinematics(corrected);
  execution_->ApplyCompatibilityPositionOffset(offset);
  // No kinematic oracle here: this is not a sync point. A standalone
  // CommitLocomotion leaves the Player ahead of the not-yet-projected state by
  // design, and the oracle is asserted where a sync completes
  // (CommitLegacyActionMotion / ProjectSimulationMovementState).
}

void Player::OffsetPosition(const Vector3 &offset) {
  // External corrections (PlayerContact) additionally assert that the action
  // state still matches its legacy oracle. Corrections evaluated inside the
  // compatibility pass cannot: the animation state is mid-update there.
  ApplyPositionCorrection(offset);
  execution_->CheckSimulationActionOracle();
}

void Player::ResetPosition(const Vector3 &newPos, const Vector3 &focusPos) {
  execution_->ResetPosition(Port(), newPos, focusPos);
}

void Player::Deactivate(const Ball& ball, football::sim::Tick now) {
  // Preserve both historical resets, including their epochs.
  execution_->Deactivate(Port(), now);
  is_active_ = false;
}

int Player::GetReactionTime_ms() const {
  // Perceptual delay is a capability of this player alone: reaction ability
  // maps linearly onto 40..80 ms. It is not a difficulty knob.
  return int(std::round(80.f - GetStat(football::player::PlayerStat::physical_reaction) * 40.f));
}

float Player::GetControlSpeed() const {
  return execution_->ControlSpeed();
}

void Player::SetControl(const PlayerControl& control) {
  execution_->SetControl(Port(), control);
}

void Player::ClearControl() {
  execution_->ClearControl(Port());
}

void Player::Process(const football::sim::PlayerTickContext& tick, const football::sim::player::PlayerObservation& observation, football::sim::player::PlayerTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
  const float distance = PrepareTick(tick, observation, touch_sink, runtime_sink);
  CommitTick(tick, distance);
}

float Player::PrepareTick(const football::sim::PlayerTickContext& tick, const football::sim::player::PlayerObservation& observation, football::sim::player::PlayerTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
  if (is_active_) {
    match_state_.desired_time_to_ball_ms =
        std::max(match_state_.desired_time_to_ball_ms - 10, 0);
    const Vector3 posBefore = execution_->CompatibilityPosition();
    execution_->RunActionTick(Port(), tick.now, tick, observation, touch_sink, runtime_sink);
    // S0.3: no adoption here. Every path already finished its own write inside
    // the action tick -- the pure-locomotion branch committed and projected, and
    // each legacy root-motion branch adopted at the point where it produced the
    // position. Adopting again would silently re-read a stale projection after
    // the Player had authority.
    execution_->CheckSimulationActionOracle();
    return (execution_->CompatibilityPosition() - posBefore).GetLength();
  }
  return 0;
}

void Player::CommitTick(const football::sim::PlayerTickContext& tick, float distance) {
  if (!is_active_) return;
  // Clock gate is sampled after authoritative touches (opening kick included).
  if (tick.half_underway) {
    match_state_.fatigue_factor_inv -=
        distance * 0.00003f * (2.0f - GetStaminaStat());
    match_state_.fatigue_factor_inv =
        clamp(match_state_.fatigue_factor_inv, 0.01f, 1.0f);
  }
  if (match_state_.cards > 1 && match_state_.card_effective_tick <= tick.now &&
      ActivePlayerCount(tick.own_team.teammates) > 1) {
    SendOff(tick.ball, tick.now, tick.send_off);
  }
}

// The ability in force. It is the base definition ability scaled by the
// current fatigue only: there is one runtime modifier, and it is a gameplay
// fact this player owns.
float Player::GetStat(football::player::PlayerStat name) const {
  return model_.attributes.get(name) * (0.7f + 0.3f * GetFatigueFactorInv());
}

float Player::GetMaxVelocity() const {
  // see the player motion constants
  return sprintVelocity * GetVelocityMultiplier();
}

PlayerMotionProfile Player::EffectiveMotionProfile() const {
  // Static ability -> capability conversion, in one place.
  PlayerMotionProfile profile = MakePlayerMotionProfile(model_);
  // Established top-speed semantics: the ability formula, deliberately with no
  // AI-difficulty or fatigue factor (see the header). Keeping the runtime part
  // here means a later behaviour change has exactly one place to land.
  profile.max_speed = GetMaxVelocity();
  return profile;
}

float Player::GetVelocityMultiplier() const {
  // see the player motion constants
  return 0.9f + model_.attributes.get(football::player::PlayerStat::physical_velocity) * 0.1f;
}

float Player::GetLastTouchBias(int decay_ms, football::sim::Tick time) const {
  if (decay_ms <= 0 || time < match_state_.last_touch_tick) return 0.0f;
  const auto age = time - match_state_.last_touch_tick;
  // Saturate before projection: absolute millisecond capacity is irrelevant.
  if (age.value > static_cast<unsigned int>(decay_ms) / football::sim::kMillisecondsPerTick) return 0.0f;
  return 1.0f - clamp(football::sim::ToMilliseconds(age) / (float)decay_ms, 0.0f, 1.0f);
}

Vector3 Player::GetPitchPosition() {
  return GetPosition();
}

void Player::Activate(const Vector3& spawn_position) {
  assert(!is_active_);
  is_active_ = true;
  // Fresh compatibility storage and the one zero-position reset happen inside
  // the executor; the placement itself is the Runtime's decision, so the
  // Player no longer knows where it was told to stand.
  execution_->Activate(Port(), spawn_position);
}


void Player::SendOff(const Ball& ball, football::sim::Tick now, football::sim::PlayerSendOffSink* send_off) {
  Deactivate(ball, now);
  // Runtime owns the roster/formation/possession follow-up: the trigger stays
  // synchronous here so the per-actor order is intact.
  if (send_off != nullptr) send_off->OnPlayerSentOff(*this);
}

float Player::GetStaminaStat() const {
  return model_.attributes.get(football::player::PlayerStat::physical_stamina);
}

void Player::ResetSituation(const Vector3 &focusPos, football::sim::Tick now) {
  execution_->ResetSituation(Port(), focusPos, now);
  SetDesiredTimeToBall_ms(0);
  triggerControlledBallCollision = false;
}

// ---------------------------------------------------------------------------
// Read-only forwards. The execution process owns these values; the Player keeps
// the gameplay-facing names so Runtime and the contact layer never depend on
// the executor type.
// ---------------------------------------------------------------------------

const std::optional<LocomotionAnimationInput>& Player::LocomotionAnimationInputSnapshot() const {
  return execution_->LocomotionAnimationInputSnapshot();
}

bool Player::LocomotionAnimationAdaptation() const {
  return execution_->LocomotionAnimationAdaptation();
}

void Player::SetLocomotionAnimationAdaptation(bool enabled) {
  execution_->SetLocomotionAnimationAdaptation(enabled);
}

void Player::SetMovementPoseBlend(bool enabled) {
  execution_->SetMovementPoseBlend(enabled);
}

bool Player::MovementPoseBlend() const { return execution_->MovementPoseBlend(); }

radian Player::GetRelBodyAngle() const { return execution_->RelBodyAngle(); }
e_Velocity Player::GetEnumVelocity() const { return execution_->EnumVelocity(); }
float Player::GetFloatVelocity() const { return execution_->FloatVelocity(); }
bool Player::IsEligibleForProceduralLocomotion(bool retaining_ball) const {
  return execution_->IsEligibleForProceduralLocomotion(retaining_ball);
}

const PlayerActionState &Player::GetSimulationActionState() const {
  return execution_->ActionState();
}

RequestOutcome Player::Play(const PlayerActionRequest& request) {
  return execution_->Play(Port(), request);
}

RequestOutcome Player::React(const PlayerReaction& reaction, const Player* ball_retainer) {
  return execution_->React(Port(), reaction, ball_retainer);
}

const std::optional<PlayerActionRequest>& Player::PendingActionRequest() const {
  return execution_->PendingActionRequest();
}

const std::optional<PlayerReaction>& Player::PendingReaction() const {
  return execution_->PendingReaction();
}

RequestOutcome Player::LastActionRequestOutcome() const {
  return execution_->LastActionRequestOutcome();
}

RequestOutcome Player::LastReactionOutcome() const {
  return execution_->LastReactionOutcome();
}

int Player::ActionRequestAttempts() const { return execution_->ActionRequestAttempts(); }
int Player::ActionRequestAccepts() const { return execution_->ActionRequestAccepts(); }
int Player::ActionRequestDefers() const { return execution_->ActionRequestDefers(); }
int Player::ActionRequestRejects() const { return execution_->ActionRequestRejects(); }
int Player::ActionRequestBuildFailures() const {
  return execution_->ActionRequestBuildFailures();
}
int Player::ActionRequestInputsConsumed() const {
  return execution_->ActionRequestInputsConsumed();
}
int Player::ReactionTripInterruptsScheduled() const {
  return execution_->ReactionTripInterruptsScheduled();
}
int Player::ActionRequestPreemptions() const {
  return execution_->ActionRequestPreemptions();
}

PoseFrame Player::Pose() const {
  if (!is_active_) return PoseFrame{};
  return execution_->Pose(Port());
}

const PlayerActionFeedback& Player::ActionFeedback() const {
  return execution_->Feedback();
}

PlayerExecutionDiagnostics Player::Diagnostics() const {
  return PlayerExecutionDiagnostics{
      execution_->AuthorityChecks(),
      execution_->AuthorityMatches(),
      execution_->AuthorityMismatches(),
      execution_->InstallChecks(),
      execution_->InstallMatches(),
      execution_->InstallMismatches(),
      execution_->InstallCursorChecks(),
      execution_->InstallCursorMatches(),
      execution_->InstallCursorMismatches(),
      execution_->TouchSelectionSuccesses()};
}

void Player::CheckSimulationActionOracle() const {
  execution_->CheckSimulationActionOracle();
}

const Vector3 &Player::GetDecayingPositionOffset() const {
  return execution_->DecayingPositionOffset();
}

float Player::GetDecayingPositionOffsetLength() const {
  return execution_->DecayingPositionOffsetLength();
}

AnimationId Player::GetCurrentAnimationId() const {
  return execution_->CurrentAnimationId();
}

PlayerBakedPoseSample Player::GetBakedPoseSample() const {
  return execution_->BakedPoseSample(Port());
}

PlayerBakedPoseSample Player::PreviewRawPose(int frame_offset) const {
  return execution_->PreviewRawPose(Port(), frame_offset);
}

bool Player::TouchPending() const { return GetSimulationActionState().IsContactPending(); }
bool Player::TouchAnim() const { return GetSimulationActionState().HasScheduledContact(); }
Vector3 Player::GetTouchPos() const { return GetSimulationActionState().contactPosition; }
int Player::GetTouchFrame() const { return GetSimulationActionState().ContactFrame(); }
int Player::GetCurrentFrame() const { return GetSimulationActionState().Frame(); }

void Player::SelectRetainAnim() { execution_->SelectRetainAnim(Port()); }
