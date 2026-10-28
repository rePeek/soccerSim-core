#include "player/player.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "football/ball/ball.hpp"
#include "player/execution/player_command.hpp"
#include "player/execution/player_control_builder.hpp"
#include "football/player/state/player_kinematics.hpp"
#include "player/execution/player_action_arbitration.hpp"

using football::ball::Ball;


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

// ANIM-2 A11.5/A11.7: the request/reaction arbitration boundary. Both record
// the outcome immediately. Only an ACCEPTED request is retained as the pending
// execution input, so a deferred intent cannot be silently executed later; the
// producer resubmits it when the window opens.
//
// `Play(ControlAction::None)` is rejected on purpose: continuous movement
// intent belongs to SetControl/PlayerMotion, and selecting a Movement clip
// through Play would not change the authoritative locomotion intent.
RequestOutcome Player::Play(const PlayerActionRequest& request) {
  if (request.action == ControlAction::None) {
    action_request_outcome_ = RequestOutcome::Rejected;
    ++action_request_attempts_;
    ++action_request_rejects_;
    return RequestOutcome::Rejected;
  }
  // Reject contract violations synchronously. Conditions that need the live
  // ball/team (inactive receiver, unsolvable trajectory, rule gate) are still
  // reported later by CommandBuildResult.
  const bool pass = football::sim::player::IsPassControlAction(request.action);
  if ((pass && !request.has_target_position &&
       request.target_player == football::player::kInvalidPlayerId) ||
      (request.action == ControlAction::Shoot &&
       !request.has_target_position &&
       request.direction.GetLength() <= 0.000001f)) {
    action_request_outcome_ = RequestOutcome::Rejected;
    ++action_request_attempts_;
    ++action_request_rejects_;
    return RequestOutcome::Rejected;
  }
  const RequestOutcome outcome =
      ArbitrateActionRequest(actionState, request.action);
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

CommandBuildResult Player::RequestCommand(const PlayerCommandInputs& inputs) {
  if (control_) return BuildPlayerCommands(*control_, *this, inputs);
  // No implicit policy in sim: absent control is an idle movement intent.
  PlayerControl idle;
  idle.player = GetID();
  idle.move_direction = GetDirectionVec();
  return BuildPlayerCommands(idle, *this, inputs);
}

// ANIM-2 A11.5: an accepted active request enters the same pipeline as the
// controller. The request is in the actor's runtime frame, so convert into the
// home value domain and reuse BuildPlayerCommands, the one translation boundary.
CommandBuildResult Player::BuildCommandQueueForRequest(
    const PlayerActionRequest& request,
    const football::sim::PlayerTickContext& tick) {
  PlayerControl control;
  control.player = GetID();
  // The request is already in the single Stadium Frame.
  control.move_direction = request.direction.Get2D();
  control.desired_speed = std::max(0.0f, request.speed);
  control.action = request.action;
  control.power = request.power;
  if (request.has_target_position)
    control.target_position = request.target_position;
  if (request.target_player != football::player::kInvalidPlayerId)
    control.target_player = request.target_player;
  const PlayerCommandInputs inputs{
      tick.ball, tick.ball_retainer, tick.pitch, tick.own_team,
      tick.authorization};
  return BuildPlayerCommands(control, *this, inputs);
}
