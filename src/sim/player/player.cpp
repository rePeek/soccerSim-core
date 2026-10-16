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

#include "sim/player/player.hpp"
#include "football/animation/library.hpp"
#include "football/pitch/pitch.hpp"

#include <cmath>
#include <cassert>
#include <cstring>

#include "football/ball/ball.hpp"
#include "football/ball/ball_timing.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/player/perception.hpp"
#include "sim/player/reachability.hpp"
#include "sim/player/roster_query.hpp"
#include "sim/player/player_action_executor.hpp"
#include "sim/player/player_locomotion.hpp"
#include "sim/player/player_motion.hpp"
#include "sim/player/player_body_facing.hpp"
#include "sim/player/locomotion_intent_scheduler.hpp"
#include "sim/player/legacy_locomotion_command.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/player/player_send_off_sink.hpp"

using football::ball::Ball;

namespace {
// Player-local ball-control estimate. It reads only the player's own motion and
// the ball's predicted movement, so it lives with the player's possession state
// rather than in a general query module.
bool ComputeHasPossession(Ball *ball, Player *player) {
  Vector3 playerMovement = player->GetMovement();

  // premature optimization ;)
  if ((player->GetPosition() - ball->state().position).GetLength() > 5.0) return false;

  if (std::fabs(ball->state().position.coords[2]) > 0.5) return false;

  bool distanceOK = true;
  float radius = 1.0f;
  Vector3 center = player->GetPosition() + player->GetMovement() * 0.05f + player->GetDirectionVec() * 0.1f;//was: 0.05f
  if ((ball->state().position.Get2D() - center).GetLength() > radius) distanceOK = false;

  bool movementOK = true;
  Vector3 ballMovement3D = (ball->Predict(football::sim::TickSpan{1}).position - ball->state().position) * 100.0f;
  if ((ballMovement3D - playerMovement).GetLength() > 6.0f) movementOK = false;

  return distanceOK && movementOK;
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
void Player::NoteDecisionPublicationCause(int cause) { pendingPublicationCause = cause; }
int &DecisionPublicationViaSimulationCadence() { static int value = 0; return value; }
int &DecisionPublicationViaLegacyOpportunityOnly() { static int value = 0; return value; }
int &DecisionPublicationWhileIneligible() { static int value = 0; return value; }
int &ReentryFreshViaSimulationCadence() { static int value = 0; return value; }
int &ReentryFreshViaLegacyOpportunityOnly() { static int value = 0; return value; }
int &ReentryFreshWhileIneligible() { static int value = 0; return value; }

int &LocomotionNegativeDecisionAgeSamples() { static int value = 0; return value; }
int &LocomotionReentryMeasurementEpoch() { static int value = 0; return value; }
void ResetLocomotionReentryAudits() {
  for (int category = 0; category < 4; ++category) {
    LocomotionReentryAuditFor(category) = LocomotionReentryAudit();
  }
  LocomotionActionExitCount() = 0;
  LocomotionNegativeDecisionAgeSamples() = 0;
  DecisionPublicationViaSimulationCadence() = 0;
  DecisionPublicationViaLegacyOpportunityOnly() = 0;
  DecisionPublicationWhileIneligible() = 0;
  ReentryFreshViaSimulationCadence() = 0;
  ReentryFreshViaLegacyOpportunityOnly() = 0;
  ReentryFreshWhileIneligible() = 0;
  // Per-player provenance is scoped by epoch, so a player resets its own audit
  // state on its next tick without iterating the roster.
  ++LocomotionReentryMeasurementEpoch();
}

void Player::CheckDecisionLocomotionIntentOracle() const {
  // Debug producer invariant; executability is checked separately.
  const PlayerCommand &command = decisionLocomotionState.command;
  assert(decisionLocomotionState.initialized && command.useDesiredMovement &&
         command.desiredFunctionType == e_FunctionType_Movement &&
         decisionLocomotionState.publishedEpoch ==
             decisionLocomotionState.continuityEpoch &&
         "executed locomotion intent must be a current-epoch Movement intent");
}

void Player::AdvanceLocomotionContinuity(bool eligible) {
  // Gameplay transition. The audit below observes eligibility and must never
  // decide the epoch, or telemetry bookkeeping would drive a controller query.
  if (!decisionLocomotionState.continuityStarted) {
    decisionLocomotionState.continuityStarted = true;
    decisionLocomotionState.wasEligibleLastTick = eligible;
    return;
  }
  if (decisionLocomotionState.wasEligibleLastTick && !eligible) {
    ++decisionLocomotionState.continuityEpoch;
  }
  decisionLocomotionState.wasEligibleLastTick = eligible;
}

void Player::NoteLocomotionReentryTick(bool eligible, bool scheduler_due,
                                          football::sim::Tick now) {
  // Measurement epoch: telemetry is transient and not serialized, so state
  // restore can rewind the match clock behind it. Scope the audit to the epoch
  // instead of letting earlier scenarios pollute it.
  if (reentryAuditEpoch != LocomotionReentryMeasurementEpoch()) {
    reentryAuditEpoch = LocomotionReentryMeasurementEpoch();
    reentryAuditStarted = false;
    locomotionExitRecorded = false;
  }
  const unsigned long long generation = decisionLocomotionAuditGeneration;
  if (!reentryAuditStarted) {
    reentryAuditStarted = true;
    wasPureLocomotionLastTick = eligible;
    resetSinceLastPlayerTick = false;
    return;
  }
  if (!eligible) {
    // An ordinary non-locomotion tick is not a re-entry. Only leaving
    // locomotion is an event, and it arms the generation comparison.
    if (wasPureLocomotionLastTick) {
      decisionGenerationAtLocomotionExit = generation;
      locomotionExitRecorded = true;
      // The epoch itself is advanced by AdvanceLocomotionContinuity(); this is
      // observation only.
      ++LocomotionActionExitCount();
    }
    wasPureLocomotionLastTick = false;
    return;
  }
  // Eligible from here on. An actor that began the audit in an action and later
  // becomes locomotion-eligible has an initial transition, not an observed action
  // re-entry; reserve category 2 for a recorded locomotion exit.
  int category = 0;
  if (resetSinceLastPlayerTick) {
    category = 3;
  } else if (wasPureLocomotionLastTick) {
    category = 1;
  } else if (locomotionExitRecorded) {
    category = 2;
  }
  LocomotionReentryAudit &audit = LocomotionReentryAuditFor(category);
  ++audit.ticks;
  if (scheduler_due) ++audit.scheduler_due; else ++audit.scheduler_not_due;
  // Shadow of the epoch rule. Continuous locomotion must never require a fresh
  // decision just because the cadence is due.
  if (decisionLocomotionState.publishedEpoch !=
      decisionLocomotionState.continuityEpoch) {
    ++audit.would_require_fresh;
    if (last_reset_tick_ && (!last_locomotion_publication_tick_ ||
        *last_reset_tick_ > *last_locomotion_publication_tick_)) {
      ++audit.stale_after_reset;
    } else if (category == 2 && locomotionExitRecorded) {
      // a3b2 no longer lets animation opportunities publish the missing axis.
      // A stale action re-entry is repaired by the forced Player Decision tick
      // later in this same Process() call, before locomotion execution.
      ++audit.stale_after_action_exit;
    } else {
      ++audit.stale_not_explained_by_reset;
    }
  }
  if (category >= 2) {
    // Reset is itself the discontinuity, so it anchors the comparison; other
    // re-entries require an observed locomotion exit.
    const bool anchor_valid = category == 3 ? resetGenerationAnchorValid
                                            : locomotionExitRecorded;
    const unsigned long long anchor = category == 3
        ? resetDecisionGeneration
        : decisionGenerationAtLocomotionExit;
    if (anchor_valid && generation != anchor) {
      ++audit.generation_advanced;
      if (lastPublicationViaSimulationCadence) {
        ++ReentryFreshViaSimulationCadence();
      } else {
        ++ReentryFreshViaLegacyOpportunityOnly();
      }
      if (lastPublicationWhileIneligible) ++ReentryFreshWhileIneligible();
    } else if (anchor_valid) {
      ++audit.generation_unchanged;
    }
  }
  if (last_locomotion_publication_tick_) {
    if (now < *last_locomotion_publication_tick_) {
      // A restorable clock cannot precede a publication in normal forward play.
      ++LocomotionNegativeDecisionAgeSamples();
    } else {
      const auto age = now - *last_locomotion_publication_tick_;
      audit.decision_age_sum += age;
      ++audit.decision_age_count;
      if (!audit.decision_age_max || age > *audit.decision_age_max) audit.decision_age_max = age;
    }
  }
  wasPureLocomotionLastTick = true;
  resetSinceLastPlayerTick = false;
}


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
// Preserve the measured 500 ms exact-rollout horizon; beyond it, the same
// analytic capability approximation applies. This is not another physics model.
constexpr football::sim::TickSpan kReachabilityExactHorizon{50};

// Bit comparison, not an epsilon: a movement mirror that is only approximately
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
               const football::player::PlayerDefinition& model,
               const FormationEntry& formation_entry,
               std::uint8_t schedule_phase,
               const AnimationLibrary& animations, const football::pitch::Pitch& pitch, football::math::Rng& rng)
    : animations_(animations), pitch_(pitch), rng_(rng),
      model_(model),
      body_profile_(football::player::MakePlayerBodyProfile(model)),
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
  tacticalSituation.forwardSpaceRating = 0;
  tacticalSituation.toGoalSpaceRating = 0;
  tacticalSituation.spaceRating = 0;
  cards = 0;
  card_effective_tick_ = {};
}

Player::~Player() = default;

void Player::Exit(football::sim::Tick now) {
  if (isActive) {
    // Preserve the former base destructor's reset/RNG window without invoking
    // roster callbacks: Team::Exit may already have deleted other players.
    SetNextResetSituationAuditContext(kResetSituationPlayerDeactivateSecond);
    ResetRuntimeState(GetPosition(), now);
    isActive = false;
  }
}

bool Player::IsKinematicMirrorConsistent() const {
  return Vector3BitsEqual(kinematicState.position, spatial_state_.position) &&
         Vector3BitsEqual(kinematicState.velocity, spatial_state_.movement) &&
         Vector3BitsEqual(kinematicState.bodyFacing,
                          spatial_state_.bodyDirectionVec) &&
         Vector3BitsEqual(groundCollider.center,
                          kinematicState.position.Get2D());
}

void Player::CheckSimulationKinematicOracle() const {
  assert(IsKinematicMirrorConsistent() &&
         "gameplay kinematic mirror diverged from the compatibility spatial state");
}

void Player::Mirror() {
  // Never-activated bench entries have no runtime state. Sent-off players
  // were activated once and must keep the historical mirror/oracle path.
  if (!legacy_state_initialized_) return;
  MirrorLegacyState();
  // S0.4: mirror through the single commit point. PlayerKinematicState::Mirror
  // keeps the historical field asymmetry on purpose -- position and velocity are
  // mirrored, facing and bodyFacing are not -- because the legacy spatial-state
  // mirror does the same. Changing that would be a coordinate-system migration,
  // not a lifecycle cleanup.
  PlayerKinematicState mirrored = kinematicState;
  mirrored.Mirror();
  CommitKinematicState(mirrored);
  CheckSimulationKinematicOracle();
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
  // here: the compatibility mirror is refreshed by the caller's sync step, and the
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
  next.position = spatial_state_.position;
  next.velocity = spatial_state_.movement;
  next.bodyFacing = spatial_state_.bodyDirectionVec;
  CommitKinematicState(next);
  CheckSimulationKinematicOracle();
}

bool Player::NoteLocomotionIntentCadence(bool legacy_opportunity, football::sim::Tick now, bool retaining_ball) {
  decisionMovementSelection = false;
  const bool due = locomotionIntentScheduler.Due(now);
  locomotionIntentDueThisTick = false;
  if (due) {
    ++PlayerLocomotionIntentDueTicks();
    // A due tick during a non-locomotion action is not consumed: querying the
    // controller there would hand locomotion an intent that is already hundreds
    // of milliseconds old by the time the action ends. The clock simply stays
    // overdue so the refresh happens on the first eligible tick.
    if (!IsEligibleForProceduralLocomotion(retaining_ball)) {
      ++PlayerLocomotionIntentDueIneligibleTicks();
    } else {
      locomotionIntentDueThisTick = true;
    }
  }
  if (legacy_opportunity) {
    ++PlayerLocomotionIntentLegacyOpportunityTicks();
    if (locomotionIntentDueThisTick) ++PlayerLocomotionIntentOverlapTicks();
  }
  return locomotionIntentDueThisTick;
}

void Player::PublishPlayerDecisionQueue(
    const PlayerCommandQueue &commands, football::sim::Tick now) {
  playerDecisionQueue.commands = commands;
  playerDecisionQueue.initialized = true;
  ++playerDecisionQueue.generation;
  playerDecisionScheduler.Commit(now);
}

void Player::CommitLocomotionIntentRefresh(const Ball& ball, football::sim::Tick now, bool retaining_ball) {
  ++HumanoidIntentRefreshCommits();
  const float distance_to_ball =
      (ball.state().position.Get2D() - kinematicState.position).GetLength();
  ++PlayerLocomotionIntentConsumedTicks();
  locomotionIntentScheduler.Schedule(
      now,
      LocomotionIntentScheduler::CadenceForDistance(
          distance_to_ball, retaining_ball));
}


// 4b': intent refresh is held back purely because execution is not pure. This is
// the conflation under measurement: the scheduler is due, but the execution gate
// blocks the refresh.
bool Player::LocomotionIntentRefreshHeldIneligible(football::sim::Tick now, bool retaining_ball) const {
  return locomotionIntentScheduler.Due(now) &&
         !IsEligibleForProceduralLocomotion(retaining_ball);
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

void Player::NoteDecisionMovementSelection(bool movement_selected) {
  decisionMovementSelection = movement_selected;
}

// c2a: the Player Decision Clock's single publication entry point. It owns the
// decision locomotion state and the publication telemetry, and never touches the
// compatibility movement command slot.
void Player::PublishDecisionLocomotionIntent(const PlayerCommand &command, football::sim::Tick now, bool retaining_ball) {
  assert(command.desiredFunctionType == e_FunctionType_Movement &&
         command.useDesiredMovement &&
         "Player Decision Clock published a non-Movement intent");
  // 4f-a1: whether this publication materially rewrites the decision already in
  // force. Only animation-owned (legacy-only) publications are counted, so the
  // number answers how often a requeue actually moved the decision clock.
  const int publication_cause = pendingPublicationCause;
  const bool decision_materially_changed =
      decisionLocomotionState.initialized &&
      MovementCommandDiffersMaterially(decisionLocomotionState.command, command);
  ++PlayerMovementCommandDirectAdoptions();
  last_locomotion_publication_tick_ = now;
  // Cause split without changing the player path: a publication on a tick where
  // the simulation cadence was not due can only have come from the animation
  // lifecycle's query opportunity.
  lastPublicationViaSimulationCadence = pendingPublicationCause == 0;
  ++DecisionPublicationCauseCount(pendingPublicationCause);
  pendingPublicationCause = 0;
  lastPublicationWhileIneligible = !IsEligibleForProceduralLocomotion(retaining_ball);
  if (lastPublicationViaSimulationCadence) {
    ++DecisionPublicationViaSimulationCadence();
  } else {
    ++DecisionPublicationViaLegacyOpportunityOnly();
  }
  if (publication_cause == 1) {
    ++LegacyOnlyDecisionPublications();
    if (decision_materially_changed) ++LegacyOnlyDecisionMaterialChanges();
    if (decisionMovementSelection) {
      ++LegacyOnlyDecisionMovementSelectionPublications();
      if (decision_materially_changed) {
        ++LegacyOnlyDecisionMovementSelectionMaterialChanges();
      }
    }
  }
  if (lastPublicationWhileIneligible) ++DecisionPublicationWhileIneligible();
  // A publication belongs to the continuity epoch it was produced in, which is
  // what makes it usable for this epoch's locomotion execution.
  decisionLocomotionState.command = command;
  decisionLocomotionState.initialized = true;
  ++decisionLocomotionAuditGeneration;
  decisionLocomotionState.publishedEpoch =
      decisionLocomotionState.continuityEpoch;
}

// Single place that turns a controller queue into a published locomotion intent.
// Returns true only when a Movement candidate with useDesiredMovement was
// published, so callers commit the scheduler refresh only on a real publication.
bool Player::PublishMovementIntentFromQueue(const PlayerCommandQueue &queue, football::sim::Tick now, bool retaining_ball) {
  for (const PlayerCommand &candidate : queue) {
    if (candidate.desiredFunctionType == e_FunctionType_Movement &&
        candidate.useDesiredMovement) {
      PublishDecisionLocomotionIntent(candidate, now, retaining_ball);
      return true;
    }
  }
  return false;
}


PlayerActionState Player::CaptureLegacyActionState() const {
  PlayerActionState legacy;
  legacy.type = anim_state_.functionType;
  legacy.elapsed = {static_cast<std::uint64_t>(anim_state_.frameNum)};
  legacy.duration = {static_cast<std::uint64_t>(GetFrameCount())};
  const Anim *anim = &anim_state_;
  if (anim->touchFrame >= 0)
    legacy.contact = football::sim::TickSpan{static_cast<std::uint64_t>(anim->touchFrame)};
  legacy.contactPosition = anim->touchPos;
  return legacy;
}

void Player::CheckSimulationActionOracle() const {
  const PlayerActionState legacy = CaptureLegacyActionState();
  const bool contactPositionMatches =
      std::memcmp(actionState.contactPosition.coords,
                  legacy.contactPosition.coords,
                  sizeof(actionState.contactPosition.coords)) == 0;
  std::string mismatch;
  if (actionState.type != legacy.type) {
    mismatch = "type";
  } else if (actionState.elapsed != legacy.elapsed) {
    mismatch = "frame";
  } else if (actionState.duration != legacy.duration) {
    mismatch = "frame count";
  } else if (actionState.contact != legacy.contact) {
    mismatch = "contact frame";
  } else if (!contactPositionMatches) {
    mismatch = "contact position bits";
  }
  assert(mismatch.empty() &&
         "authoritative action state diverged from legacy oracle");
}

void Player::BeginSimulationAction() {
  const Anim *anim = &anim_state_;
  PlayerActionDefinition definition;
  definition.type = anim_state_.functionType;
  definition.duration = {static_cast<std::uint64_t>(GetFrameCount())};
  if (anim->touchFrame >= 0)
    definition.contact = football::sim::TickSpan{static_cast<std::uint64_t>(anim->touchFrame)};
  definition.contactPosition = anim->touchPos;
  last_begun_definition_ = definition;
  last_begun_start_frame_ = anim_state_.frameNum;
  PlayerActionExecutor::Begin(actionState, definition);

  // ResetPosition can deliberately start an idle animation at a non-zero
  // legacy frame. Establish that initial cursor once; normal ticks never derive
  // executor time from the compatibility projection.
  const football::sim::TickSpan initial_elapsed{static_cast<std::uint64_t>(anim_state_.frameNum)};
  if (initial_elapsed.value > 0) {
    PlayerActionExecutor::Step(actionState, initial_elapsed);
  }
  CheckSimulationActionOracle();
}
// ANIM-1 A6.4 step 4: single Player entry point for touch-animation selection.
  // The evaluation runs against the Player's own touch math; the Player owns
  // the decision and retains the result.
int Player::SelectTouchAction(football::sim::Tick now, const football::sim::PlayerTickContext &tick,
                              std::span<MentalImage> history, const DataSet &sorted_data_set,
                              bool use_desired_movement, const Vector3 &desired_direction,
                              float desired_velocity_float, bool use_desired_body_direction,
                              const Vector3 &desired_body_direction_rel,
                              football::sim::TouchChoice &choice,
                              e_InterruptAnim local_interrupt_anim,
                              bool prefer_pass_and_shot,
                              bool allow_pre_touch_rotation_smuggle,
                              bool use_continuous_ball_check) {
  // ANIM-1 A6.4 step 9c: the loop is Player-owned. The action-execution state
  // supplies the spatial facts and the A8.0 reference-motion kernel.
  // not-yet-migrated spatial facts and the A8.0 reference-motion kernel.
  const int anim_id = SelectBestTouchAnim(
      now, tick, history, sorted_data_set, use_desired_movement, desired_direction,
      desired_velocity_float, use_desired_body_direction, desired_body_direction_rel,
      choice, local_interrupt_anim, prefer_pass_and_shot,
      allow_pre_touch_rotation_smuggle, use_continuous_ball_check);
  if (anim_id != -1) {
    touch_choice_ = choice;
    ++touch_selection_successes_;
  }
  return anim_id;
}

bool Player::CommitSelectedAction(int selected_anim_id,
                                  const football::sim::TouchChoice &touch_choice,
                                  const PlayerCommand &command,
                                  const football::sim::PlayerTickContext &tick,
                                  football::sim::Tick now,
                                  std::span<MentalImage> history,
                                  e_InterruptAnim local_interrupt_anim,
                                  bool material_candidate,
                                  int start_frame) {
  // ANIM-1 A6.4 step 7b: the Player owns the install dispatch. The compat
  // projection writes the legacy currentAnim; the Player then installs through
  // the one install entry, and only then records the scheduled-contact audit.
  ProjectSelectedActionCompat(selected_anim_id, touch_choice, command, tick, now,
                              history, local_interrupt_anim, material_candidate,
                              start_frame);
  InstallSelectedAction(CurrentSelectedAction());
  RecordSelectedContactAudit(command);
  return true;
}


void Player::InstallSelectedAction(const football::sim::SelectedAction &selection) {
  // ANIM-1 A6.3b.3 (second cut): the Player is now the authority for the action
  // definition - it is built from the Player-owned `SelectedAction`, not read
  // back from the compatibility projection. The compat projection is synced by
  // selector before this call and is cross-checked below. The entry never
  // re-selects a clip or vetoes an interrupt.
  installed_action_ = selection;

  PlayerActionDefinition definition;
  definition.type = selection.function_type;
  definition.duration = {static_cast<std::uint64_t>(selection.frame_count)};
  if (selection.touch_frame >= 0)
    definition.contact = football::sim::TickSpan{static_cast<std::uint64_t>(selection.touch_frame)};
  definition.contactPosition = selection.touch_position;
  last_begun_definition_ = definition;
  last_begun_start_frame_ = selection.start_frame;
  PlayerActionExecutor::Begin(actionState, definition);
  if (selection.start_frame > 0) {
    PlayerActionExecutor::Step(
        actionState, football::sim::TickSpan{static_cast<std::uint64_t>(selection.start_frame)});
  }
  CheckSimulationActionOracle();

  // Oracle during migration: the compat projection must still agree with
  // the Player-owned selection, otherwise the two authorities have drifted.

  // Same-tick consistency: the committed cursor, the scheduled contact and the
  // compat projection must all agree with the selection immediately, so an
  // install followed by a sample in the same tick cannot be one tick off.
  ++install_cursor_checks_;
  const bool cursor_same =
      actionState.Frame() == selection.start_frame &&
      actionState.FrameCount() == selection.frame_count &&
      actionState.HasScheduledContact() == (selection.touch_frame >= 0) &&
      actionState.ContactFrame() == selection.touch_frame;
  if (cursor_same) {
    ++install_cursor_matches_;
  } else {
    ++install_cursor_mismatches_;
  }
  ++install_checks_;
  const Anim *anim = &anim_state_;
  const bool same =
      selection.function_type == anim_state_.functionType &&
      selection.frame_count == GetFrameCount() &&
      selection.start_frame == anim_state_.frameNum &&
      selection.touch_frame == anim->touchFrame &&
      selection.touch_position == anim->touchPos;
  if (same) {
    ++install_matches_;
  } else {
    ++install_mismatches_;
  }

}



void Player::StepSimulationAction(football::sim::TickSpan elapsed) {
  PlayerActionExecutor::Step(actionState, elapsed);
  CheckSimulationActionOracle();
}

void Player::UpdatePlayback(AnimationId clip_id, int frame, float base_angle,
                            int blend_ticks) {
  const AnimationClip& clip = animations_.Get(static_cast<std::uint32_t>(clip_id));
  playback_state_.Play(clip, frame, base_angle,
                         playback_blend_enabled_ ? blend_ticks : 0);
}

bool Player::IsEligibleForProceduralLocomotion(bool retaining_ball) const {
  return actionState.IsPureLocomotion(retaining_ball);
}

void Player::AdvanceActionTick() {
  // ANIM-1 A6.3c.2: one real time advance. The PlayerActionState cursor is the
  // authority; the compatibility integer frame is a read-only projection.
  PlayerActionExecutor::Step(actionState, football::sim::TickSpan{1});
  anim_state_.frameNum = actionState.Frame();
  CheckSimulationActionOracle();
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
  // `installed_action_` stays valid and the RNG-chosen start frame is carried
  // in the selection (RNG draws and order unchanged).
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
  // CommitLocomotion leaves the Player ahead of the not-yet-mirrored projection by
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
  // Preserve both historical resets, including their RNG draws and epochs.
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

// ANIM-2 A11.5/A11.7: the request/reaction arbitration boundary. Both record
// the outcome immediately. Only an ACCEPTED request is retained as the pending
// execution input, so a deferred intent cannot be silently executed later; the
// producer resubmits it when the window opens.
//
// `Play(Move)` is rejected on purpose: continuous movement intent belongs to
// SetControl/PlayerMotion, and selecting a Movement clip through Play would not
// change the authoritative locomotion intent. Use SetControl for movement.
RequestOutcome Player::Play(const PlayerActionRequest& request) {
  if (request.kind == ActionRequestKind::Move) {
    action_request_outcome_ = RequestOutcome::Rejected;
    ++action_request_attempts_;
    ++action_request_rejects_;
    return RequestOutcome::Rejected;
  }
  const RequestOutcome outcome = ArbitrateActionRequest(actionState, request.kind);
  action_request_outcome_ = outcome;
  ++action_request_attempts_;
  if (outcome == RequestOutcome::Accepted) {
    ++action_request_accepts_;
    pending_action_request_ = request;
  } else if (outcome == RequestOutcome::Deferred) {
    ++action_request_defers_;
  } else {
    ++action_request_rejects_;
  }
  return outcome;
}

// ANIM-2 A11.5/A11.7: recording the pending reaction is ATOMIC with the trip
// staging. A trip that loses the guard is not retained, so it cannot clobber a
// first accepted trip, and a non-trip reaction never replaces a staged trip
// (the first valid trip wins). The physical collision fact is already applied
// by the contact phase; only the animation reaction is staged here.
RequestOutcome Player::React(const PlayerReaction& reaction,
                             const Player* ball_retainer) {
  const RequestOutcome outcome = ArbitrateReaction(actionState, reaction.kind);
  reaction_outcome_ = outcome;
  if (outcome == RequestOutcome::Rejected) return outcome;
  if (reaction.kind == ReactionKind::Trip) {
    if (!this->StagePassiveTrip(reaction, ball_retainer)) {
      // Already staged or refused by the guard: keep the existing reaction.
      reaction_outcome_ = RequestOutcome::Deferred;
      return RequestOutcome::Deferred;
    }
    pending_reaction_ = reaction;
  }
  // Non-trip reactions have no animation execution yet, so they never touch the
  // pending executable slot; they are recorded through the outcome only.
  return outcome;
}

std::optional<PlayerReaction> Player::ConsumePendingReaction() {
  std::optional<PlayerReaction> reaction = pending_reaction_;
  pending_reaction_.reset();
  return reaction;
}

std::optional<PlayerActionRequest> Player::ConsumePendingActionRequest() {
  std::optional<PlayerActionRequest> request = pending_action_request_;
  pending_action_request_.reset();
  return request;
}

void Player::RequestCommand(PlayerCommandQueue &commandQueue, const PlayerCommandInputs& inputs) {
  if (control_) {
    commandQueue = BuildPlayerCommands(*control_, *this, inputs);
  } else {
    // No implicit policy in sim: absent control is an idle movement intent.
    PlayerControl idle;
    idle.player = GetID();
    idle.move_direction = GetDirectionVec();
    if (team_facts_.dynamic_side != (team_id_ == 0 ? -1 : 1))
      idle.move_direction.Mirror();
    commandQueue = BuildPlayerCommands(idle, *this, inputs);
  }
}

void Player::Process(const football::sim::PlayerTickContext& tick, std::span<MentalImage> history, football::sim::AcceptedTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
  const float distance = PrepareTick(tick, history, touch_sink, runtime_sink);
  CommitTick(tick, distance);
}

float Player::PrepareTick(const football::sim::PlayerTickContext& tick, std::span<MentalImage> history, football::sim::AcceptedTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
  if (isActive) {
    desiredTimeToBall_ms = std::max(desiredTimeToBall_ms - 10, 0);
    if (tick.play_authorized) {
      if (football::sim::player_timing::StaggeredRefreshDue(
          tick.now, football::sim::player_timing::kTacticalRefresh,
          football::sim::TickSpan{schedule_phase_})) {
        _CalculateTacticalSituation(tick, history);
      }
    }
    Vector3 posBefore = spatial_state_.position;
    RunActionTick(tick.now, tick, history, touch_sink, runtime_sink);
    // S0.3: no adoption here. Every path already finished its own write inside
    // the action tick -- the pure-locomotion branch committed and projected, and
    // each legacy root-motion branch adopted at the point where it produced the
    // position. Adopting again would silently re-read a stale projection after
    // the Player had authority.
    CheckSimulationActionOracle();
    return (spatial_state_.position - posBefore).GetLength();
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
    SendOff(tick.ball, tick.now, tick.rng, tick.send_off);
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
  Vector3 pos = GetPosition();
  if (!team_facts_.on_original_side) pos.Mirror();
  return pos;
}

void Player::Activate() {
  assert(!isActive);
  isActive = true;
  // A fresh actor must start from fresh compatibility storage, exactly as a
  // newly-constructed Humanoid did before the storage became Player-owned.
  anim_state_ = Anim{};
  spatial_state_ = SpatialState{};
  // The old Humanoid constructor performed one zero-position reset before the
  // explicit formation-position reset. Keep both, in order, so the RNG draws
  // and the resulting idle entry frame are unchanged.
  ResetLegacyPosition(Vector3(0), Vector3(0));
  ResetLegacyPosition(
      GetFormationEntry().position * 25 *
          Vector3(-team_facts_.dynamic_side, -team_facts_.dynamic_side, 0),
      Vector3(0));
  legacy_state_initialized_ = true;
  CommitLegacyActionMotion();
  // ANIM-1 A6.3c.1: a reset is a special action install, not an out-of-band
  // cursor write. Route it through the same Player install entry so
  // `installed_action_` stays valid and the RNG-chosen start frame is carried
  // in the selection (RNG draws and order unchanged).
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

void Player::UpdatePossessionStats(Ball& ball, const football::sim::TeamMatchState& opponent, football::sim::Tick now, const Player* retainer, const Player* designated_own) {
  timeNeededToGetToBall_previous_ms = timeNeededToGetToBall_ms;
  const e_FunctionType action_type = GetCurrentFunctionType();
  if (actionState.IsPureLocomotion(retainer == this)) {
    // H3e1c-3c: solve on the staggered 100 ms cadence, retaining the capability
    // estimate between refreshes. Do not fall back to the legacy heuristic on
    // those nine intermediate ticks (P1c); that would change the AI's model.
    ++PlayerReachabilityEligibleTicks();
    const bool scheduled_refresh = football::sim::player_timing::StaggeredRefreshDue(
        now, kReachabilityRefresh,
        football::sim::TickSpan{schedule_phase_});
    // First eligible tick after an action must not reuse an old action estimate.
    const bool entering_pure_locomotion = GetSimulationActionState().elapsed.value == 0;
    if (scheduled_refresh || entering_pure_locomotion) {
      ++PlayerReachabilityRefreshes();
      PlayerLocomotionParameters locomotion_parameters;
      locomotion_parameters.maxSpeed = GetMaxVelocity();
      // Capability belief, not another simulator: exact near-horizon rollout,
      // analytic approximation beyond 500 ms. The exact estimator remains the
      // measurement oracle; this approximation need not reproduce its belief.
      const PlayerLocomotionReach reach =
          PlayerLocomotion::EstimateEarliestInterceptHybrid(
              GetKinematicState(),
              [&ball](football::sim::TickSpan horizon) { return ball.Predict(horizon).position; },
              locomotion_parameters, GetMaxVelocity(),
              football::sim::ball_timing::kPredictionHorizon,
              kReachabilityExactHorizon,
              kLocomotionUsualReachRadius, kLocomotionOptimisticReachRadius,
              /*steady_state=*/true);
      // Possession ranking also admits continuous action estimates; project only
      // this discrete solver's result, never quantize the mixed estimate storage.
      timeNeededToGetToBall_ms = static_cast<unsigned int>(football::sim::ToMilliseconds(
          reach.usual.value_or(football::sim::ball_timing::kPredictionHorizon)));
      timeNeededToGetToBall_optimistic_ms = static_cast<unsigned int>(football::sim::ToMilliseconds(
          reach.optimistic.value_or(football::sim::ball_timing::kPredictionHorizon)));
    } else {
      ++PlayerReachabilityReuses();
    }
  } else {
    // Non-locomotion actions keep the legacy heuristic because their execution
    // still follows animation root motion.
    timeNeededToGetToBall_ms = std::max(
        static_cast<unsigned int>(football::sim::ToMilliseconds(football::sim::ball_timing::kPredictionHorizon)),
        (unsigned int)(std::round(
            (ball.Predict(football::sim::ball_timing::kPredictionHorizon - football::sim::TickSpan{1}).position.Get2D() -
             (GetPosition() + GetMovement() * 0.2f)).GetLength() /
            (GetMaxVelocity() * 0.75f) * 1000)));
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
    using football::sim::TickSpan;
    using football::sim::ToMilliseconds;
    TickSpan start{};
    if ((action_type == e_FunctionType_ShortPass ||
         action_type == e_FunctionType_LongPass ||
         action_type == e_FunctionType_HighPass ||
         action_type == e_FunctionType_Shot) && !TouchPending()) {
      start = TickSpan{50};
    }
    bool refine = false;
    TickSpan step{1};
    TickSpan previous{};
    const bool precise = designated_own == this;
    for (auto candidate = start; candidate < football::sim::ball_timing::kPredictionHorizon; candidate += step) {
      const auto ms = static_cast<unsigned int>(ToMilliseconds(candidate));
      if (ball.Predict(candidate).position.coords[2] < 1.5f) {
        const auto result = football::sim::player::GetTimeNeededForDistance_ms(
            GetPosition(), GetMovement(), ball.Predict(candidate).position.Get2D(),
            GetMaxVelocity(), precise, candidate);
        if (result.optimistic_ms <= ms && ms < timeNeededToGetToBall_optimistic_ms) {
          timeNeededToGetToBall_optimistic_ms = ms;
        }
        if (result.usual_ms <= ms) {
          if (!refine) {
            candidate = previous;
            step = TickSpan{1};
            refine = true;
          } else {
            timeNeededToGetToBall_ms = ms;
            break;
          }
        }
      }
      if (!refine) {
        const float balldist = (GetPosition() - ball.Predict(candidate).position.Get2D()).GetLength() + 0.2f;
        const float maxBallVelo = 50;
        const unsigned int timeToGo_ms = int(std::round((balldist / maxBallVelo) * 1000.0f));
        // Preserve the historical adaptive grid search (round first, then floor).
        step = TickSpan{static_cast<unsigned int>(clamp(timeToGo_ms, 10, 500)) / 10};
      } else step = TickSpan{1};
      previous = candidate;
    }
  }
  if (TouchAnim() && TouchPending()) {
    unsigned int animTimeToBall_ms = (GetTouchFrame() - GetCurrentFrame()) * 10;
    timeNeededToGetToBall_ms = std::min(timeNeededToGetToBall_ms, animTimeToBall_ms);
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
  }
  if (timeNeededToGetToBall_ms < defaultTouchOffset_ms) {
    timeNeededToGetToBall_ms = NormalizedClamp(((GetPosition() + GetMovement() * (defaultTouchOffset_ms * 0.001)) - ball.Predict(football::sim::TickSpan{defaultTouchOffset_ms / football::sim::kMillisecondsPerTick}).position.Get2D()).GetLength(), 0.0f, 0.6f) * defaultTouchOffset_ms;
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
  }
  if ((action_type == e_FunctionType_ShortPass ||
       action_type == e_FunctionType_LongPass ||
       action_type == e_FunctionType_HighPass ||
       action_type == e_FunctionType_Shot) && !TouchPending()) {
    hasPossession = false;
  } else {
    hasPossession = ComputeHasPossession(&ball, this);
  }
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



void Player::SendOff(const Ball& ball, football::sim::Tick now, football::math::Rng& rng, football::sim::PlayerSendOffSink* send_off) {
  // Preserve the historical draw even though its message was removed.
  (void)rng.Uniform(0, 3);
  Deactivate(ball, now);
  // Runtime owns the roster/formation/possession follow-up: the trigger stays
  // synchronous here so the per-actor order (and the RNG draw above) is intact.
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
  tacticalSituation.forwardSpaceRating = 0;
  tacticalSituation.toGoalSpaceRating = 0;
  tacticalSituation.spaceRating = 0;
}

void Player::_CalculateTacticalSituation(const football::sim::PlayerTickContext& tick, std::span<MentalImage> history) {
  // Sample only when needed. Reaction estimates keep their sub-tick precision;
  // the history sampler, not stored Player state, owns nearest-capture rounding.
  const bool own_touch = tick.last_touch_player == this && lastTouchType != e_TouchType_Accidental;
  const MentalImage *mentalImage = own_touch
      ? football::sim::observation::SampleMentalImage(history, football::sim::TickSpan{})
      : football::sim::observation::SampleMentalImage(history, std::chrono::milliseconds{GetReactionTime_ms()});
  assert(mentalImage);
  assert(IsActive());
  float time_sec = 0.5f;
  Vector3 checkPos = GetPosition() + Vector3(-team_facts_.dynamic_side, 0, 0) * sprintVelocity * time_sec;
  tacticalSituation.forwardSpaceRating = football::sim::player::CalculateFreeSpace(tick.now, mentalImage, team_id_, checkPos, 5.0f, time_sec);
  time_sec = 0.1f;
  checkPos = GetPosition() + GetMovement() * time_sec;
  tacticalSituation.spaceRating = football::sim::player::CalculateFreeSpace(tick.now, mentalImage, team_id_, checkPos, 5.0f, time_sec);
  tacticalSituation.forwardRating =
      1.0f - clamp((Vector3(pitch_.half_length() * -team_facts_.dynamic_side, 0, 0) - GetPosition()).GetLength() /
                       (pitch_.half_length() * 2.0f), 0.0f, 1.0f);
  tacticalSituation.forwardRating = std::pow(tacticalSituation.forwardRating, 1.5f);
}
