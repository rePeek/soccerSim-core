#include "player/execution/player_execution.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

#include "football/ball/ball.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/player_action_arbitration.hpp"
#include "player/execution/player_command.hpp"
#include "player/execution/player_control_builder.hpp"
#include "player/execution/player_execution_diagnostics.hpp"
#include "player/execution/player_diagnostics.hpp"

using football::ball::Ball;

void PlayerExecution::CheckDecisionLocomotionIntentOracle() const {
  // Debug producer invariant; executability is checked separately.
  const PlayerCommand &command = state_.decisionLocomotionState.command;
  assert(state_.decisionLocomotionState.initialized && command.useDesiredMovement &&
         command.desiredFunctionType == e_FunctionType_Movement &&
         state_.decisionLocomotionState.publishedEpoch ==
             state_.decisionLocomotionState.continuityEpoch &&
         "executed locomotion intent must be a current-epoch Movement intent");
}

void PlayerExecution::AdvanceLocomotionContinuity(bool eligible) {
  // Gameplay transition. The audit below observes eligibility and must never
  // decide the epoch, or telemetry bookkeeping would drive a controller query.
  if (!state_.decisionLocomotionState.continuityStarted) {
    state_.decisionLocomotionState.continuityStarted = true;
    state_.decisionLocomotionState.wasEligibleLastTick = eligible;
    return;
  }
  if (state_.decisionLocomotionState.wasEligibleLastTick && !eligible) {
    ++state_.decisionLocomotionState.continuityEpoch;
  }
  state_.decisionLocomotionState.wasEligibleLastTick = eligible;
}

void PlayerExecution::NoteLocomotionReentryTick(bool eligible, bool scheduler_due,
                                          football::sim::Tick now) {
  // Measurement epoch: telemetry is transient and not serialized, so state
  // restore can rewind the match clock behind it. Scope the audit to the epoch
  // instead of letting earlier scenarios pollute it.
  if (state_.reentryAuditEpoch != LocomotionReentryMeasurementEpoch()) {
    state_.reentryAuditEpoch = LocomotionReentryMeasurementEpoch();
    state_.reentryAuditStarted = false;
    state_.locomotionExitRecorded = false;
  }
  const unsigned long long generation = state_.decisionLocomotionAuditGeneration;
  if (!state_.reentryAuditStarted) {
    state_.reentryAuditStarted = true;
    state_.wasPureLocomotionLastTick = eligible;
    state_.resetSinceLastPlayerTick = false;
    return;
  }
  if (!eligible) {
    // An ordinary non-locomotion tick is not a re-entry. Only leaving
    // locomotion is an event, and it arms the generation comparison.
    if (state_.wasPureLocomotionLastTick) {
      state_.decisionGenerationAtLocomotionExit = generation;
      state_.locomotionExitRecorded = true;
      // The epoch itself is advanced by AdvanceLocomotionContinuity(); this is
      // observation only.
      ++LocomotionActionExitCount();
    }
    state_.wasPureLocomotionLastTick = false;
    return;
  }
  // Eligible from here on. An actor that began the audit in an action and later
  // becomes locomotion-eligible has an initial transition, not an observed action
  // re-entry; reserve category 2 for a recorded locomotion exit.
  int category = 0;
  if (state_.resetSinceLastPlayerTick) {
    category = 3;
  } else if (state_.wasPureLocomotionLastTick) {
    category = 1;
  } else if (state_.locomotionExitRecorded) {
    category = 2;
  }
  LocomotionReentryAudit &audit = LocomotionReentryAuditFor(category);
  ++audit.ticks;
  if (scheduler_due) ++audit.scheduler_due; else ++audit.scheduler_not_due;
  // Shadow of the epoch rule. Continuous locomotion must never require a fresh
  // decision just because the cadence is due.
  if (state_.decisionLocomotionState.publishedEpoch !=
      state_.decisionLocomotionState.continuityEpoch) {
    ++audit.would_require_fresh;
    if (state_.last_reset_tick_ && (!state_.last_locomotion_publication_tick_ ||
        *state_.last_reset_tick_ > *state_.last_locomotion_publication_tick_)) {
      ++audit.stale_after_reset;
    } else if (category == 2 && state_.locomotionExitRecorded) {
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
    const bool anchor_valid = category == 3 ? state_.resetGenerationAnchorValid
                                            : state_.locomotionExitRecorded;
    const unsigned long long anchor = category == 3
        ? state_.resetDecisionGeneration
        : state_.decisionGenerationAtLocomotionExit;
    if (anchor_valid && generation != anchor) {
      ++audit.generation_advanced;
      if (state_.lastPublicationViaSimulationCadence) {
        ++ReentryFreshViaSimulationCadence();
      } else {
        ++ReentryFreshViaLegacyOpportunityOnly();
      }
      if (state_.lastPublicationWhileIneligible) ++ReentryFreshWhileIneligible();
    } else if (anchor_valid) {
      ++audit.generation_unchanged;
    }
  }
  if (state_.last_locomotion_publication_tick_) {
    if (now < *state_.last_locomotion_publication_tick_) {
      // A restorable clock cannot precede a publication in normal forward play.
      ++LocomotionNegativeDecisionAgeSamples();
    } else {
      const auto age = now - *state_.last_locomotion_publication_tick_;
      audit.decision_age_sum += age;
      ++audit.decision_age_count;
      if (!audit.decision_age_max || age > *audit.decision_age_max) audit.decision_age_max = age;
    }
  }
  state_.wasPureLocomotionLastTick = true;
  state_.resetSinceLastPlayerTick = false;
}

bool PlayerExecution::NoteLocomotionIntentCadence(bool legacy_opportunity, football::sim::Tick now, bool retaining_ball) {
  state_.decisionMovementSelection = false;
  const bool due = state_.locomotionIntentScheduler.Due(now);
  state_.locomotionIntentDueThisTick = false;
  if (due) {
    ++PlayerLocomotionIntentDueTicks();
    // A due tick during a non-locomotion action is not consumed: querying the
    // controller there would hand locomotion an intent that is already hundreds
    // of milliseconds old by the time the action ends. The clock simply stays
    // overdue so the refresh happens on the first eligible tick.
    if (!IsEligibleForProceduralLocomotion(retaining_ball)) {
      ++PlayerLocomotionIntentDueIneligibleTicks();
    } else {
      state_.locomotionIntentDueThisTick = true;
    }
  }
  if (legacy_opportunity) {
    ++PlayerLocomotionIntentLegacyOpportunityTicks();
    if (state_.locomotionIntentDueThisTick) ++PlayerLocomotionIntentOverlapTicks();
  }
  return state_.locomotionIntentDueThisTick;
}

void PlayerExecution::PublishPlayerDecisionQueue(
    const PlayerCommandQueue &commands, football::sim::Tick now) {
  state_.playerDecisionQueue.commands = commands;
  state_.playerDecisionQueue.initialized = true;
  ++state_.playerDecisionQueue.generation;
  state_.playerDecisionScheduler.Commit(now);
}

void PlayerExecution::CommitLocomotionIntentRefresh(const Ball& ball, football::sim::Tick now, bool retaining_ball) {
  ++HumanoidIntentRefreshCommits();
  const float distance_to_ball =
      (ball.state().position.Get2D() - port().GetKinematicState().position).GetLength();
  ++PlayerLocomotionIntentConsumedTicks();
  state_.locomotionIntentScheduler.Schedule(
      now,
      LocomotionIntentScheduler::CadenceForDistance(
          distance_to_ball, retaining_ball));
}

// 4b': intent refresh is held back purely because execution is not pure. This is
// the conflation under measurement: the scheduler is due, but the execution gate
// blocks the refresh.
bool PlayerExecution::LocomotionIntentRefreshHeldIneligible(football::sim::Tick now, bool retaining_ball) const {
  return state_.locomotionIntentScheduler.Due(now) &&
         !IsEligibleForProceduralLocomotion(retaining_ball);
}

// c2a: the Player Decision Clock's single publication entry point. It owns the
// decision locomotion state and the publication telemetry, and never touches the
// compatibility movement command slot.
void PlayerExecution::PublishDecisionLocomotionIntent(const PlayerCommand &command, football::sim::Tick now, bool retaining_ball) {
  assert(command.desiredFunctionType == e_FunctionType_Movement &&
         command.useDesiredMovement &&
         "Player Decision Clock published a non-Movement intent");
  // 4f-a1: whether this publication materially rewrites the decision already in
  // force. Only animation-owned (legacy-only) publications are counted, so the
  // number answers how often a requeue actually moved the decision clock.
  const int publication_cause = state_.pendingPublicationCause;
  const bool decision_materially_changed =
      state_.decisionLocomotionState.initialized &&
      MovementCommandDiffersMaterially(state_.decisionLocomotionState.command, command);
  ++PlayerMovementCommandDirectAdoptions();
  state_.last_locomotion_publication_tick_ = now;
  // Cause split without changing the player path: a publication on a tick where
  // the simulation cadence was not due can only have come from the animation
  // lifecycle's query opportunity.
  state_.lastPublicationViaSimulationCadence = state_.pendingPublicationCause == 0;
  ++DecisionPublicationCauseCount(state_.pendingPublicationCause);
  state_.pendingPublicationCause = 0;
  state_.lastPublicationWhileIneligible = !IsEligibleForProceduralLocomotion(retaining_ball);
  if (state_.lastPublicationViaSimulationCadence) {
    ++DecisionPublicationViaSimulationCadence();
  } else {
    ++DecisionPublicationViaLegacyOpportunityOnly();
  }
  if (publication_cause == 1) {
    ++LegacyOnlyDecisionPublications();
    if (decision_materially_changed) ++LegacyOnlyDecisionMaterialChanges();
    if (state_.decisionMovementSelection) {
      ++LegacyOnlyDecisionMovementSelectionPublications();
      if (decision_materially_changed) {
        ++LegacyOnlyDecisionMovementSelectionMaterialChanges();
      }
    }
  }
  if (state_.lastPublicationWhileIneligible) ++DecisionPublicationWhileIneligible();
  // A publication belongs to the continuity epoch it was produced in, which is
  // what makes it usable for this epoch's locomotion execution.
  state_.decisionLocomotionState.command = command;
  state_.decisionLocomotionState.initialized = true;
  ++state_.decisionLocomotionAuditGeneration;
  state_.decisionLocomotionState.publishedEpoch =
      state_.decisionLocomotionState.continuityEpoch;
}

// Single place that turns a controller queue into a published locomotion intent.
// Returns true only when a Movement candidate with useDesiredMovement was
// published, so callers commit the scheduler refresh only on a real publication.
bool PlayerExecution::PublishMovementIntentFromQueue(const PlayerCommandQueue &queue, football::sim::Tick now, bool retaining_ball) {
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
RequestOutcome PlayerExecution::Play(PlayerExecutionPort& owner, const PlayerActionRequest& request) {
  PortScope scope(*this, owner);
  if (request.action == ControlAction::None) {
    state_.action_request_outcome_ = RequestOutcome::Rejected;
    ++state_.action_request_attempts_;
    ++state_.action_request_rejects_;
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
    state_.action_request_outcome_ = RequestOutcome::Rejected;
    ++state_.action_request_attempts_;
    ++state_.action_request_rejects_;
    return RequestOutcome::Rejected;
  }
  const RequestOutcome outcome =
      ArbitrateActionRequest(state_.actionState, request.action);
  state_.action_request_outcome_ = outcome;
  ++state_.action_request_attempts_;
  if (outcome == RequestOutcome::Accepted) {
    ++state_.action_request_accepts_;
    state_.pending_action_request_ = request;
  } else if (outcome == RequestOutcome::Deferred) {
    ++state_.action_request_defers_;
  } else {
    ++state_.action_request_rejects_;
  }
  return outcome;
}

// ANIM-2 A11.5/A11.7: recording the pending reaction is ATOMIC with the trip
// staging. A trip that loses the guard is not retained, so it cannot clobber a
// first accepted trip, and a non-trip reaction never replaces a staged trip
// (the first valid trip wins). The physical collision fact is already applied
// by the contact phase; only the animation reaction is staged here.
RequestOutcome PlayerExecution::React(PlayerExecutionPort& owner, const PlayerReaction& reaction,
                      const Player* ball_retainer) {
  PortScope scope(*this, owner);
  const RequestOutcome outcome = ArbitrateReaction(state_.actionState, reaction.kind);
  state_.reaction_outcome_ = outcome;
  if (outcome == RequestOutcome::Rejected) return outcome;
  if (reaction.kind == ReactionKind::Trip) {
    if (!StagePassiveTrip(reaction, ball_retainer)) {
      // Already staged or refused by the guard: keep the existing reaction.
      state_.reaction_outcome_ = RequestOutcome::Deferred;
      return RequestOutcome::Deferred;
    }
    state_.pending_reaction_ = reaction;
  }
  // Non-trip reactions have no animation execution yet, so they never touch the
  // pending executable slot; they are recorded through the outcome only.
  return outcome;
}

std::optional<PlayerReaction> PlayerExecution::ConsumePendingReaction() {
  std::optional<PlayerReaction> reaction = state_.pending_reaction_;
  state_.pending_reaction_.reset();
  return reaction;
}

std::optional<PlayerActionRequest> PlayerExecution::ConsumePendingActionRequest() {
  std::optional<PlayerActionRequest> request = state_.pending_action_request_;
  state_.pending_action_request_.reset();
  return request;
}

CommandBuildResult PlayerExecution::RequestCommand(const PlayerCommandInputs& inputs) {
  if (state_.control_) return BuildPlayerCommands(*state_.control_, *self(), inputs);
  // No implicit policy in sim: absent control is an idle movement intent.
  PlayerControl idle;
  idle.player = port().GetID();
  idle.move_direction = port().GetDirectionVec();
  return BuildPlayerCommands(idle, *self(), inputs);
}

// ANIM-2 A11.5: an accepted active request enters the same pipeline as the
// controller. The request is in the actor's runtime frame, so convert into the
// home value domain and reuse BuildPlayerCommands, the one translation boundary.
CommandBuildResult PlayerExecution::BuildCommandQueueForRequest(
    const PlayerActionRequest& request,
    const football::sim::PlayerTickContext& tick) {
  PlayerControl control;
  control.player = port().GetID();
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
  return BuildPlayerCommands(control, *self(), inputs);
}
