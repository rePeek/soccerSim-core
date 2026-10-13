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
  return Vector3BitsEqual(kinematicState.position, humanoid->GetPosition()) &&
         Vector3BitsEqual(kinematicState.velocity, humanoid->GetMovement()) &&
         Vector3BitsEqual(kinematicState.facing, humanoid->GetDirectionVec()) &&
         Vector3BitsEqual(kinematicState.bodyFacing,
                          humanoid->GetBodyDirectionVec()) &&
         FloatBitsEqual(kinematicState.speed,
                        kinematicState.velocity.GetLength()) &&
         Vector3BitsEqual(groundCollider.center,
                          kinematicState.position.Get2D());
}

void Player::CheckSimulationKinematicOracle() const {
  assert(IsKinematicMirrorConsistent() &&
         "gameplay kinematic mirror diverged from Humanoid spatial state");
}

void Player::Mirror() {
  // Never-activated bench entries have no runtime pose. Sent-off players still
  // own their Humanoid and must keep the historical mirror/oracle path.
  if (!humanoid) return;
  humanoid->Mirror();
  kinematicState.Mirror();
  groundCollider.Mirror();
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
        command.desiredDirection.Get2D().GetNormalized(tick_start.facing) *
        clamp(command.desiredVelocityFloat, 0.f, max_speed);
  }
  // P2b: the whole capability profile is in force now. max_speed keeps the
  // established GetMaxVelocity() semantics; acceleration/braking follow
  // physical_acceleration and turn_rate follows physical_agility. This is an
  // intentional trajectory change (a more athletic player now accelerates and
  // turns faster instead of only being allowed a higher top speed). Fatigue and
  // AI difficulty are still deliberately not baked in: they remain per-tick
  // runtime modifiers, so a tired player can still slow down.
  PlayerMotionProfile profile = MakePlayerMotionProfile(model_);
  profile.max_speed = max_speed;

  PlayerKinematicState next =
      PlayerMotion::StepContinuous(tick_start, profile, input.desiredVelocity,
                                   input.idleFacing, dt)
          .state;
  // Torso model consumes the resulting locomotion facing and writes only
  // bodyFacing; order and parameters are unchanged from the legacy path.
  PlayerBodyFacingInput bodyInput;
  bodyInput.desiredFacing = next.facing;
  if (command.useDesiredLookAt) {
    bodyInput.desiredFacing =
        (command.desiredLookAt - next.position).Get2D().GetNormalized(next.facing);
  }
  PlayerBodyFacing::Step(next, bodyInput, PlayerBodyFacingParameters{}, dt);
  CommitKinematicState(next);
  return next;
}

void Player::CommitKinematicState(const PlayerKinematicState& next) {
  // Full-state commit: the caller computed every field, including `speed`; this
  // point only installs it and keeps the ground collider consistent. No oracle
  // here: the Humanoid mirror is refreshed by the caller's sync step, and the
  // two sides are momentarily allowed to disagree.
  kinematicState = next;
  groundCollider.SetCenter(kinematicState.position);
}

void Player::CommitLegacyActionMotion() {
  // S0.1: adopting the Humanoid's motion is legitimate ONLY for the legacy
  // animation-root-motion paths (and the lifecycle resets that re-read its
  // pose). Pure locomotion never comes through here, so the Humanoid cannot
  // become the authority for a tick the Player already computed. The commit
  // still goes through the single write point, which also keeps the ground
  // collider in step.
  PlayerKinematicState next;
  next.position = humanoid->GetPosition();
  next.velocity = humanoid->GetMovement();
  next.facing = humanoid->GetDirectionVec();
  next.bodyFacing = humanoid->GetBodyDirectionVec();
  next.speed = next.velocity.GetLength();
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
  legacy.type = humanoid->GetCurrentFunctionType();
  legacy.elapsed = {static_cast<std::uint64_t>(humanoid->GetFrameNum())};
  legacy.duration = {static_cast<std::uint64_t>(humanoid->GetFrameCount())};
  const Anim *anim = humanoid->GetCurrentAnim();
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
  const Anim *anim = humanoid->GetCurrentAnim();
  PlayerActionDefinition definition;
  definition.type = humanoid->GetCurrentFunctionType();
  definition.duration = {static_cast<std::uint64_t>(humanoid->GetFrameCount())};
  if (anim->touchFrame >= 0)
    definition.contact = football::sim::TickSpan{static_cast<std::uint64_t>(anim->touchFrame)};
  definition.contactPosition = anim->touchPos;
  PlayerActionExecutor::Begin(actionState, definition);

  // ResetPosition can deliberately start an idle animation at a non-zero
  // legacy frame. Establish that initial cursor once; normal ticks never
  // derive executor time from Humanoid.
  const football::sim::TickSpan initial_elapsed{static_cast<std::uint64_t>(humanoid->GetFrameNum())};
  if (initial_elapsed.value > 0) {
    PlayerActionExecutor::Step(actionState, initial_elapsed);
  }
  CheckSimulationActionOracle();
}

void Player::StepSimulationAction(football::sim::TickSpan elapsed) {
  PlayerActionExecutor::Step(actionState, elapsed);
  CheckSimulationActionOracle();
}

bool Player::IsEligibleForProceduralLocomotion(bool retaining_ball) const {
  return actionState.IsPureLocomotion(retaining_ball);
}


void Player::ResetPosition(const Vector3 &newPos, const Vector3 &focusPos) {
  humanoid->ResetPosition(newPos, focusPos);
  CommitLegacyActionMotion();
  BeginSimulationAction();
}

void Player::ApplyPositionCorrection(const Vector3 &offset) {
  // S0.2: the Player is the authority for a corrected position, wherever the
  // correction is *evaluated* (PlayerContact, or still the goalkeeper boundary
  // clamps inside the Humanoid pass). Commit it first, then let the legacy
  // Humanoid adopt the same offset as animation/motion bookkeeping (startPos,
  // previous position, smuggle offsets) without re-deriving the Player state.
  //
  // Only the position changes. Velocity, facing and bodyFacing stay exactly the
  // values the previous commit installed, and `speed` is deliberately not
  // recomputed: a position correction is an instantaneous displacement, not new
  // motion. That is also what the legacy path produced, since the Humanoid never
  // changed its movement/direction for an offset.
  PlayerKinematicState corrected = kinematicState;
  corrected.position += offset;
  CommitKinematicState(corrected);
  humanoid->ApplyPositionOffset(offset);
  // No kinematic oracle here: this is not a sync point. A standalone
  // CommitLocomotion leaves the Player ahead of the not-yet-mirrored Humanoid by
  // design, and the oracle is asserted where a sync completes
  // (CommitLegacyActionMotion / ProjectSimulationMovementState).
}

void Player::OffsetPosition(const Vector3 &offset) {
  // External corrections (PlayerContact) additionally assert that the action
  // state still matches its legacy oracle. Corrections evaluated inside the
  // Humanoid pass cannot: the animation state is mid-update there.
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
    Vector3 posBefore = CastHumanoid()->GetPosition();
    CastHumanoid()->Process(tick.now, tick, history, touch_sink, runtime_sink);
    CommitLegacyActionMotion();
    CheckSimulationActionOracle();
    return (CastHumanoid()->GetPosition() - posBefore).GetLength();
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
  // see humanoidbase's physics function
  return sprintVelocity * GetVelocityMultiplier();
}

float Player::GetVelocityMultiplier() const {
  // see humanoid_utils' physics function
  return 0.9f + model_.attributes.get(football::player::PlayerStat::physical_velocity) * 0.1f;
}

float Player::GetLastTouchBias(int decay_ms, football::sim::Tick time) {
  if (decay_ms <= 0 || time < last_touch_tick_) return 0.0f;
  const auto age = time - last_touch_tick_;
  // Saturate before projection: absolute millisecond capacity is irrelevant.
  if (age.value > static_cast<unsigned int>(decay_ms) / football::sim::kMillisecondsPerTick) return 0.0f;
  return 1.0f - clamp(football::sim::ToMilliseconds(age) / (float)decay_ms, 0.0f, 1.0f);
}

void Player::ResetRuntimeState(const Vector3 &focusPos, football::sim::Tick now) {
  last_touch_tick_ = {};
  lastTouchType = e_TouchType_None;
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
    humanoid->ResetSituation(focusPos);
    CommitLegacyActionMotion();
    BeginSimulationAction();
  }
  resetSituationAuditContext = kResetSituationUnspecified;
}

Humanoid *Player::CastHumanoid() { return humanoid.get(); }


Vector3 Player::GetPitchPosition() {
  Vector3 pos = GetPosition();
  if (!team_facts_.on_original_side) pos.Mirror();
  return pos;
}

void Player::Activate() {
  assert(!isActive);
  isActive = true;
  humanoid.reset(new Humanoid(this, animations_, rng_));
  CastHumanoid()->ResetPosition(
      GetFormationEntry().position * 25 *
          Vector3(-team_facts_.dynamic_side, -team_facts_.dynamic_side, 0),
      Vector3(0));
  CommitLegacyActionMotion();
  BeginSimulationAction();
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
