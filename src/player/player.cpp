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
#include "football/animation/library.hpp"
#include "football/pitch/pitch.hpp"

#include <cmath>
#include <cassert>
#include <cstring>

#include "football/ball/ball.hpp"
#include "football/ball/ball_timing.hpp"
#include "player/execution/player_motion_constants.hpp"
#include "player/execution/reachability.hpp"
#include "player/execution/roster_query.hpp"
#include "player/execution/player_action_executor.hpp"
#include "player/execution/player_locomotion.hpp"
#include "football/player/execution/player_motion.hpp"
#include "football/player/execution/player_body_facing.hpp"
#include "player/execution/locomotion_intent_scheduler.hpp"
#include "player/execution/player_control_builder.hpp"
#include "player/execution/player_send_off_sink.hpp"

using football::ball::Ball;

namespace {

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

}  // namespace


int &SimulationOnlyGateMismatchForResetContext(int context) {
  static int records[kResetSituationCallContextCount] = {};
  return records[context];
}

const char *ResetSituationCallContextName(int context) {
  switch (context) {
    case kResetSituationInitialBeforeFirstPlayerTick: return "initial";
    case kResetSituationRuntime: return "runtime";
    case kResetSituationPlayerDeactivateFirst: return "player_deactivate_first";
    case kResetSituationPlayerDeactivateSecond: return "player_deactivate_second";
    default: return "unknown";
  }
}
int &MovementOracleConsumesForSource(int source) {
  static int records[5] = {};
  return records[source];
}
int &DecisionLocomotionIntentPresentTicks() { static int value = 0; return value; }
int &DecisionLocomotionIntentMissingTicks() { static int value = 0; return value; }
int &DecisionLocomotionIntentMissingForSource(int source) {
  static int records[5] = {};
  return records[source];
}
int &ActionCoupledLegacyMismatch() { static int value = 0; return value; }
int &DecisionLocomotionIntentMissingForSourceLegacyGateFalse(int source) {
  static int records[5] = {};
  return records[source];
}
int &LocomotionActionExitCount() { static int value = 0; return value; }
int &ContinuityRepairAttempts() { static int value = 0; return value; }
int &ContinuityRepairPublications() { static int value = 0; return value; }
int &ContinuityRepairCandidatesMissing() { static int value = 0; return value; }
int &DecisionPublicationCauseCount(int cause) { static int records[3] = {}; return records[cause]; }





int &DecisionLocomotionIntentPresentTicksLegacyGateFalse() {
  static int value = 0;
  return value;
}
int &DecisionLocomotionIntentMissingTicksLegacyGateFalse() {
  static int value = 0;
  return value;
}
LocomotionReentryAudit &LocomotionReentryAuditFor(int category) {
  static LocomotionReentryAudit records[4];
  return records[category];
}
const char *LocomotionReentryCategoryName(int category) {
  switch (category) {
    case 0: return "initial";
    case 1: return "continuous";
    case 2: return "action_reentry";
    case 3: return "reset_reentry";
    default: return "unknown";
  }
}

namespace {

// Execute every 10 ms; refresh the intercept belief every 100 ms, staggered
// by the dense match-local index. Between refreshes the estimate is retained.
constexpr football::sim::TickSpan kReachabilityRefresh{10};

// Bit comparison, not an epsilon: a movement projection that is only approximately
// right is a synchronization bug, and masking it would let a later procedural
// producer inherit an inconsistent baseline.
bool Vector3BitsEqual(const Vector3 &a, const Vector3 &b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

bool FloatBitsEqual(float a, float b) {
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

}  // namespace

Player::Player(int team_id, football::sim::TeamSide side, float ai_difficulty,
               football::player::PlayerDefinition model,
               const FormationEntry& formation_entry,
               std::uint8_t schedule_phase,
               const AnimationLibrary& animations, const football::pitch::Pitch& pitch)
    : animations_(animations), pitch_(pitch),
      model_(std::move(model)),
      body_profile_(football::player::MakePlayerBodyProfile(model_)),
      schedule_phase_(schedule_phase % 10),
      team_id_(team_id),
      side_(side),
      ai_difficulty_(ai_difficulty),
      formationEntry_(formation_entry) {
  last_touch_tick_ = {};
  lastTouchType = e_TouchType_None;
  fatigueFactorInv = 1.0;
  SetDesiredTimeToBall_ms(0);
  triggerControlledBallCollision = false;
  cards = 0;
  card_effective_tick_ = {};
}

Player::~Player() = default;

void Player::Exit(football::sim::Tick now) {
  if (isActive) {
    // Preserve the former base destructor's reset window without invoking
    // roster callbacks: Team::Exit may already have deleted other players.
    SetNextResetSituationAuditContext(kResetSituationPlayerDeactivateSecond);
    ResetRuntimeState(GetPosition(), now);
    isActive = false;
  }
}

bool Player::IsSpatialProjectionConsistent() const {
  return Vector3BitsEqual(kinematicState.position, legacy_spatial_state_.position) &&
         Vector3BitsEqual(kinematicState.velocity, legacy_spatial_state_.movement) &&
         Vector3BitsEqual(kinematicState.bodyFacing,
                          legacy_spatial_state_.bodyDirectionVec) &&
         Vector3BitsEqual(groundCollider.center,
                          kinematicState.position.Get2D());
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
  // LOCO-2.3b: capture the snapshot the animation adapter reads later this
  // tick. next already reflects bodyFacing, so the adapter sees the committed
  // motion without re-running the physics model.
  locomotion_animation_input_ =
      MakeLocomotionAnimationInput(tick_start, next, input.desiredVelocity, body_profile_);
  CommitKinematicState(next);
  return next;
}

void Player::CommitKinematicState(const PlayerKinematicState& next) {
  // Full-state commit: the caller computed every field, including `speed`; this
  // point only installs it and keeps the ground collider consistent. No oracle
  // here: the compatibility projection is refreshed by the caller's sync step, and the
  // two sides are momentarily allowed to disagree.
  kinematicState = next;
  groundCollider.SetCenter(kinematicState.position);
}

void Player::CommitLegacyActionMotion() {
  // S0.1: adopting the baked action motion is legitimate ONLY for the legacy
  // animation-root-motion paths (and the lifecycle resets that re-read the
  // compatibility pose). Pure locomotion never comes through here.
  // become the authority for a tick the Player already computed. The commit
  // still goes through the single write point, which also keeps the ground
  // collider in step.
  PlayerKinematicState next;
  next.position = legacy_spatial_state_.position;
  next.velocity = legacy_spatial_state_.movement;
  next.bodyFacing = legacy_spatial_state_.bodyDirectionVec;
  CommitKinematicState(next);
  CheckSimulationKinematicOracle();
}






static int t4opp_queries = 0, t4opp_with_candidate = 0;
static int t4opp_cand_not_due = 0, t4opp_cand_due_ineligible = 0, t4opp_cand_due_eligible = 0;
static int t4opp_nocand_not_due = 0, t4opp_nocand_other = 0;

void DumpQueryOpportunities() {
  printf("4opp-SUMMARY queries=%d with_candidate=%d\n", t4opp_queries, t4opp_with_candidate);
  printf("4opp-SUMMARY candidate: not_due=%d due+ineligible=%d due+eligible=%d\n",
         t4opp_cand_not_due, t4opp_cand_due_ineligible, t4opp_cand_due_eligible);
  printf("4opp-SUMMARY no_candidate: not_due=%d other=%d\n", t4opp_nocand_not_due,
         t4opp_nocand_other);
  fflush(stdout);
}
void Player::NoteControllerQuery(bool had_movement_candidate, football::sim::Tick now, bool retaining_ball) {
  const bool due = locomotionIntentScheduler.Due(now);
  const bool eligible = IsEligibleForProceduralLocomotion(retaining_ball);
  ++t4opp_queries;
  if (had_movement_candidate) {
    ++t4opp_with_candidate;
    if (!due) ++t4opp_cand_not_due;
    else if (!eligible) ++t4opp_cand_due_ineligible;
    else ++t4opp_cand_due_eligible;
  } else {
    if (!due) ++t4opp_nocand_not_due;
    else ++t4opp_nocand_other;
  }
}















bool Player::IsEligibleForProceduralLocomotion(bool retaining_ball) const {
  return actionState.IsPureLocomotion(retaining_ball);
}


void Player::ResetPosition(const Vector3 &newPos, const Vector3 &focusPos) {
  ResetLegacyPosition(newPos, focusPos);
  // ANIM-1.1: re-placement clears any pending active/passive request.
  pending_action_request_.reset();
  playback_state_.Reset();
  pending_reaction_.reset();
  pending_passive_trip_.reset();
  CommitLegacyActionMotion();
  // ANIM-1 A6.3c.1: a reset is a special action install, not an out-of-band
  // cursor write. Route it through the same Player install entry so
  // `installed_action_` stays valid and the deterministic entry frame is
  // carried in the selection.
  InstallSelectedAction(CurrentSelectedAction());
}

void Player::ApplyPositionCorrection(const Vector3 &offset) {
  // S0.2: the Player is the authority for a corrected position, wherever the
  // correction is *evaluated* (PlayerContact, or still the goalkeeper boundary
  // clamps inside the action tick). Commit it first, then let the compatibility
  // bookkeeping adopt the same offset (startPos, previous position, smuggle
  // offsets) without re-deriving the Player state.
  // previous position, smuggle offsets) without re-deriving the Player state.
  //
  // Only the position changes. Velocity, facing and bodyFacing stay exactly the
  // values the previous commit installed, and `speed` is deliberately not
  // recomputed: a position correction is an instantaneous displacement, not new
  // motion. That is also what the legacy path produced, since the projection never
  // changed its movement/direction for an offset.
  PlayerKinematicState corrected = kinematicState;
  corrected.position += offset;
  CommitKinematicState(corrected);
  ApplyLegacyPositionOffset(offset);
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
  CheckSimulationActionOracle();
}

void Player::SetNextResetSituationAuditContext(int context) {
  resetSituationAuditContext = context;
}

void Player::Deactivate(const Ball& ball, football::sim::Tick now) {
  SetNextResetSituationAuditContext(kResetSituationPlayerDeactivateFirst);
  ResetSituation(GetPosition(), now);
  // Preserve both historical resets, including their epochs.
  SetNextResetSituationAuditContext(kResetSituationPlayerDeactivateSecond);
  ResetSituation(GetPosition(), now);
  isActive = false;
}

int Player::GetReactionTime_ms() {
  int reaction = int(std::round(80.f - GetStat(football::player::PlayerStat::physical_reaction) * 40.f));
  reaction += (1.f - ai_difficulty_) * 100;
  return reaction;
}

float Player::GetControlSpeed() {
  if (control_) return control_->desired_speed;
  return 0.f;
}






void Player::Process(const football::sim::PlayerTickContext& tick, const football::sim::player::PlayerObservation& observation, football::sim::player::PlayerTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
  const float distance = PrepareTick(tick, observation, touch_sink, runtime_sink);
  CommitTick(tick, distance);
}

float Player::PrepareTick(const football::sim::PlayerTickContext& tick, const football::sim::player::PlayerObservation& observation, football::sim::player::PlayerTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
  if (isActive) {
    desiredTimeToBall_ms = std::max(desiredTimeToBall_ms - 10, 0);
    Vector3 posBefore = legacy_spatial_state_.position;
    RunActionTick(tick.now, tick, observation, touch_sink, runtime_sink);
    // S0.3: no adoption here. Every path already finished its own write inside
    // the action tick -- the pure-locomotion branch committed and projected, and
    // each legacy root-motion branch adopted at the point where it produced the
    // position. Adopting again would silently re-read a stale projection after
    // the Player had authority.
    CheckSimulationActionOracle();
    return (legacy_spatial_state_.position - posBefore).GetLength();
  }
  return 0;
}

void Player::CommitTick(const football::sim::PlayerTickContext& tick, float distance) {
  if (!isActive) return;
  // Clock gate is sampled after authoritative touches (opening kick included).
  if (tick.half_underway) {
    fatigueFactorInv -= distance * 0.00003f * (2.0f - GetStaminaStat());
    fatigueFactorInv = clamp(fatigueFactorInv, 0.01f, 1.0f);
  }
  if (cards > 1 && card_effective_tick_ <= tick.now &&
      ActivePlayerCount(tick.own_team.teammates) > 1) {
    SendOff(tick.ball, tick.now, tick.send_off);
  }
}

float Player::GetStat(football::player::PlayerStat name) const {
  float multiplier = 0.3f + 0.7f * ai_difficulty_;
  multiplier *= 0.7f + 0.3f * GetFatigueFactorInv();
  return model_.attributes.get(name) * multiplier;
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
  if (decay_ms <= 0 || time < last_touch_tick_) return 0.0f;
  const auto age = time - last_touch_tick_;
  // Saturate before projection: absolute millisecond capacity is irrelevant.
  if (age.value > static_cast<unsigned int>(decay_ms) / football::sim::kMillisecondsPerTick) return 0.0f;
  return 1.0f - clamp(football::sim::ToMilliseconds(age) / (float)decay_ms, 0.0f, 1.0f);
}

void Player::ResetRuntimeState(const Vector3 &focusPos, football::sim::Tick now) {
  last_touch_tick_ = {};
  lastTouchType = e_TouchType_None;
  // ANIM-1.1: a reset must not leave a stale active/passive request behind.
  pending_action_request_.reset();
  playback_state_.Reset();
  pending_reaction_.reset();
  pending_passive_trip_.reset();
  if (IsActive()) {
    // The reset itself is the discontinuity, so it anchors the generation that
    // a later reset re-entry compares against.
    resetDecisionGeneration = decisionLocomotionAuditGeneration;
    resetGenerationAnchorValid = true;
    last_reset_tick_ = now;
    resetSinceLastPlayerTick = true;
    // A reset is also a continuity break, so any earlier intent is invalid.
    ++decisionLocomotionState.continuityEpoch;
    if (resetSituationAuditContext == kResetSituationUnspecified) {
      resetSituationAuditContext = hasProcessedPlayerTick
          ? kResetSituationRuntime
          : kResetSituationInitialBeforeFirstPlayerTick;
    }
    ResetLegacySituation(focusPos);
    CommitLegacyActionMotion();
    // ANIM-1 A6.3c.1: the runtime reset is the same special action install.
    InstallSelectedAction(CurrentSelectedAction());
  }
  resetSituationAuditContext = kResetSituationUnspecified;
}



Vector3 Player::GetPitchPosition() {
  return GetPosition();
}

void Player::Activate() {
  assert(!isActive);
  isActive = true;
  // A fresh actor must start from fresh compatibility storage, exactly as a
  // newly-constructed Humanoid did before the storage became Player-owned.
  legacy_anim_state_ = Anim{};
  legacy_spatial_state_ = SpatialState{};
  // The old Humanoid constructor performed one zero-position reset before the
  // explicit formation-position reset. Keep both, in order, so the resulting
  // idle entry frame is unchanged.
  ResetLegacyPosition(Vector3(0), Vector3(0));
  ResetLegacyPosition(
      GetFormationEntry().position * 25 *
          Vector3(-team_facts_.defending_direction, -team_facts_.defending_direction, 0),
      Vector3(0));
  legacy_state_initialized_ = true;
  CommitLegacyActionMotion();
  // ANIM-1 A6.3c.1: a reset is a special action install, not an out-of-band
  // cursor write. Route it through the same Player install entry so
  // `installed_action_` stays valid and the deterministic entry frame is
  // carried in the selection.
  InstallSelectedAction(CurrentSelectedAction());
  SetDynamicFormationEntry(GetFormationEntry());
}

bool Player::HasPossession() const { return hasPossession; }
bool Player::HasBestPossession() const { return hasBestPossession; }
bool Player::HasUniquePossession() const { return hasUniquePossession; }

bool Player::AllowLastDitch(bool includingPossessionAmount) const {
  if (includingPossessionAmount && team_facts_.possession_amount < 1.0f) return true;
  return (GetTimeNeededToGetToBall_optimistic_ms() * 1.7f + 800 < GetTimeNeededToGetToBall_ms());
}

void Player::UpdatePossessionStats(Ball& ball, const football::player::PlayerTeamMatchFacts& opponent, football::sim::Tick now, const Player* retainer, const Player* designated_own) {
  timeNeededToGetToBall_previous_ms = timeNeededToGetToBall_ms;
  const e_FunctionType action_type = GetCurrentFunctionType();
  const bool pure_locomotion = actionState.IsPureLocomotion(retainer == this);
  bool recompute = false;
  if (pure_locomotion) {
    // H3e1c-3c: solve on the staggered 100 ms cadence, retaining the capability
    // estimate between refreshes. Do not fall back to the legacy heuristic on
    // those nine intermediate ticks (P1c); that would change the AI's model.
    ++PlayerReachabilityEligibleTicks();
    const bool scheduled_refresh = football::sim::player_timing::StaggeredRefreshDue(
        now, kReachabilityRefresh,
        football::sim::TickSpan{schedule_phase_});
    // First eligible tick after an action must not reuse an old action estimate.
    const bool entering_pure_locomotion = GetSimulationActionState().elapsed.value == 0;
    recompute = scheduled_refresh || entering_pure_locomotion;
    if (recompute) ++PlayerReachabilityRefreshes();
    else ++PlayerReachabilityReuses();
  }
  // P19.4: the actor's own capability is one pure value. The cross-team
  // possession comparison below stays Runtime-side arbitration.
  football::sim::player::PlayerReachabilityInput reachability;
  reachability.kinematic = GetKinematicState();
  reachability.max_velocity = GetMaxVelocity();
  reachability.action_type = action_type;
  reachability.pure_locomotion = pure_locomotion;
  reachability.recompute = recompute;
  reachability.precise = designated_own == this;
  reachability.touch_anim = TouchAnim();
  reachability.touch_pending = TouchPending();
  reachability.touch_frame = GetTouchFrame();
  reachability.current_frame = GetCurrentFrame();
  reachability.previous_usual_ms = timeNeededToGetToBall_ms;
  reachability.previous_optimistic_ms = timeNeededToGetToBall_optimistic_ms;
  reachability.ball = &ball;
  const football::sim::player::PlayerReachabilityFacts facts =
      football::sim::player::ComputePlayerReachability(reachability);
  timeNeededToGetToBall_ms = facts.usual_ms;
  timeNeededToGetToBall_optimistic_ms = facts.optimistic_ms;
  hasPossession = facts.locally_controllable;
  this->hasBestPossession = hasPossession && opponent.time_to_ball_ms > this->GetTimeNeededToGetToBall_ms();
  this->hasUniquePossession = hasPossession && !opponent.has_possession;
  if (retainer == this) {
    timeNeededToGetToBall_ms = 1;
    timeNeededToGetToBall_optimistic_ms = 1;
    SetDesiredTimeToBall_ms(timeNeededToGetToBall_ms);
    hasPossession = true;
    hasBestPossession = true;
    hasUniquePossession = true;
  } else if (retainer != 0) {
    hasPossession = false;
    hasBestPossession = false;
    hasUniquePossession = false;
  }
}

float Player::GetClosestOpponentDistance(const PlayerTeamView& opponent) const {
  Player *opp = football::sim::player::FindClosestPlayer(opponent.teammates, GetPosition());
  return opp->GetPosition().GetDistance(GetPosition());
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
  ResetRuntimeState(focusPos, now);
  hasPossession = false;
  hasBestPossession = false;
  hasUniquePossession = false;
  timeNeededToGetToBall_ms = 1000;
  timeNeededToGetToBall_optimistic_ms = 1000;
  SetDesiredTimeToBall_ms(0);
  triggerControlledBallCollision = false;
}
