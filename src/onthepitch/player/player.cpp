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

#include "player.hpp"
#include "core/physics/player_movement.hpp"

#include <cmath>

#include "../match.hpp"
#include "../team.hpp"

#include "controller/elizacontroller.hpp"
#include "controller/strategies/strategy.hpp"


#include "../../base/geometry/triangle.hpp"

namespace {

// H3e1c-3c planner cadence for pure locomotion. Execution runs the locomotion
// model every 10 ms; the intercept solver runs every 100 ms per actor,
// staggered by stable id so the cost is spread across ticks. This is a
// sampling policy, not a second physics model: between refreshes the estimate
// is merely stale. Lowering it later trades CPU for freshness without touching
// locomotion semantics.
constexpr int kReachabilityRefreshTicks = 10;

// Near-horizon region of the reachability model that keeps exact locomotion
// rollouts. Chosen from the H sweep (0..1500 ms) of the final estimator: the
// reachability classification is identical for every setting, so this is purely
// a fidelity-versus-cost knob for the reported time. Its cost minimum is at
// 500 ms (0.0196 ms per call against 0.0202 at 700), and 500 ms already carries
// p99 510 ms with at most one sub-second decision flip, so the lower end of the
// measured knee is used.
constexpr int kReachabilityExactHorizon_ms = 500;

}  // namespace

Player::Player(Team *team, PlayerData *playerData)
    : PlayerBase(team->GetMatch(), playerData), team(team) {
  DO_VALIDATION;
  SetDesiredTimeToBall_ms(0);

  triggerControlledBallCollision = false;

  tacticalSituation.forwardSpaceRating = 0;
  tacticalSituation.toGoalSpaceRating = 0;
  tacticalSituation.spaceRating = 0;

  cards = 0;

  cardEffectiveTime_ms = 0;
}

Player::~Player() {
  DO_VALIDATION;
}

Humanoid *Player::CastHumanoid() {
  DO_VALIDATION;
  return static_cast<Humanoid *>(humanoid.get());
}

ElizaController *Player::CastController() {
  DO_VALIDATION;
  return static_cast<ElizaController *>(controller.get());
}

int Player::GetTeamID() const {
  return team->GetID();
}

Vector3 Player::GetPitchPosition() {
  DO_VALIDATION;
  Vector3 pos = GetPosition();
  if (!team->onOriginalSide()) {
    pos.Mirror();
  }
  return pos;
}

Team *Player::GetTeam() {
  DO_VALIDATION;
  return team;
}

void Player::Activate(std::shared_ptr<AnimCollection> animCollection,
                      bool lazyPlayer) {
  DO_VALIDATION;

  assert(!isActive);

  isActive = true;

  humanoid.reset(new Humanoid(this, animCollection));

  controller.reset(new ElizaController(match, lazyPlayer));
  CastController()->SetPlayer(this);
  CastHumanoid()->ResetPosition(
      GetFormationEntry().position * 25 *
          Vector3(-team->GetDynamicSide(), -team->GetDynamicSide(), 0),
      Vector3(0));
  SynchronizeKinematicState();
  BeginSimulationAction();
  SetDynamicFormationEntry(GetFormationEntry());
}

void Player::Deactivate() {
  DO_VALIDATION;
  SetNextResetSituationAuditContext(kResetSituationPlayerDeactivateFirst);
  ResetSituation(GetPosition());
  if (ExternalController()) {
    DO_VALIDATION;
    team->DeselectPlayer(this); // don't want any humangamer to have control of this player anymore
  }

  PlayerBase::Deactivate();
  GetTeam()->UpdateDesignatedTeamPossessionPlayer();
}

FormationEntry Player::GetFormationEntry() {
  DO_VALIDATION;
  return team->GetFormationEntry(this);
}

bool Player::HasPossession() const {
  return hasPossession;
}

bool Player::HasBestPossession() const {
  return hasBestPossession;
}

bool Player::HasUniquePossession() const {
  return hasUniquePossession;
}

bool Player::AllowLastDitch(bool includingPossessionAmount) const {
  if (includingPossessionAmount && team->GetTeamPossessionAmount() < 1.0f) return true; // why team possession amount and not player's? answer: because we don't have that info here (todo: fix that)
  return (GetTimeNeededToGetToBall_optimistic_ms() * 1.7f + 800 < GetTimeNeededToGetToBall_ms());
}

float Player::GetAverageVelocity(float timePeriod_sec) {
  DO_VALIDATION;
  assert((int)timePeriod_sec > 0);
  unsigned int logSize = positionHistoryPerSecond.size();
  if (logSize == 0) return 0;
  Vector3 prevPos;
  float totalDistance = 0;
  unsigned int count = 0;
  while (count <= (unsigned int)timePeriod_sec) {
    DO_VALIDATION;
    Vector3 pos = positionHistoryPerSecond.at(logSize - 1 - count);
    if (count > 0) totalDistance += (pos - prevPos).GetLength();
    count++;
    if (logSize - count == 0) break;
    prevPos = pos;
  }
  return totalDistance / timePeriod_sec; // don't divide by count, since lack of entries should not influence average
}

void Player::UpdatePossessionStats() {
  DO_VALIDATION;
  timeNeededToGetToBall_previous_ms = timeNeededToGetToBall_ms;

  const e_FunctionType action_type = GetCurrentFunctionType();

  if (IsEligibleForProceduralLocomotion()) {
    // H3e1c-3c: pure locomotion reachability comes from the capability model
    // rather than the legacy distance heuristic. The solver is deterministic
    // but not free, so it runs on a staggered 100 ms cadence; the refresh phase
    // is a pure function of the simulation clock and the stable id, so no
    // scheduler state has to be serialized and a load resumes on the same
    // schedule.
    //
    // P1c: between refreshes the previous estimate is RETAINED. Earlier this
    // branch fell through to the default heuristic on every non-refresh tick,
    // which meant nine ticks out of ten fed the AI a different model entirely:
    // same capability model plus a stale estimate is the contract, and a
    // different model is not.
    ++PlayerReachabilityEligibleTicks();
    const int reachability_tick =
        static_cast<int>(match->GetActualTime_ms() / 10);
    const bool scheduled_refresh =
        (reachability_tick + GetStableID()) % kReachabilityRefreshTicks == 0;
    // Force a refresh right after entering pure locomotion, so the first ticks
    // do not retain a value produced for a previous non-locomotion action.
    // elapsedTime_ms == 0 is derived from the action state, so this keeps the
    // schedule a pure function of deterministic simulation state.
    const bool entering_pure_locomotion =
        GetSimulationActionState().elapsedTime_ms == 0;
    if (scheduled_refresh || entering_pure_locomotion) {
      ++PlayerReachabilityRefreshes();
      PlayerLocomotionParameters locomotion_parameters;
      locomotion_parameters.maxSpeed = GetMaxVelocity();
      // Capability estimate, not a second simulator. Near horizon it is the
      // exact locomotion rollout, so a close decision keeps a bounded error;
      // beyond the horizon it is the analytic model derived from the same
      // locomotion parameters. Measured against the exact oracle: MAE 64 ms,
      // p50 10 ms, p99 490 ms, worst case 700 ms, and no "exact says reachable
      // but the model says no" flips at all.
      //
      // It is deliberately allowed to differ from the exact rollout: the AI
      // consumes a capability belief, and a later perception layer can turn
      // exactly this difference into individual anticipation. The exact
      // estimator stays as the oracle this one is measured against.
      const PlayerLocomotionReach reach =
          PlayerLocomotion::EstimateEarliestInterceptHybrid(
              GetKinematicState(),
              [this](int ms) { return match->GetBall()->Predict(ms); },
              locomotion_parameters, GetMaxVelocity(),
              static_cast<int>(ballPredictionSize_ms),
              kReachabilityExactHorizon_ms,
              kLocomotionUsualReachRadius,
              kLocomotionOptimisticReachRadius,
              /*steady_state=*/true);
      timeNeededToGetToBall_ms =
          reach.usual_ms >= 0 ? static_cast<unsigned int>(reach.usual_ms)
                              : ballPredictionSize_ms;
      timeNeededToGetToBall_optimistic_ms =
          reach.optimistic_ms >= 0
              ? static_cast<unsigned int>(reach.optimistic_ms)
              : ballPredictionSize_ms;
    } else {
      // Deliberately retain the previous capability estimate: same model,
      // merely stale.
      ++PlayerReachabilityReuses();
    }
  } else {
    // Non-locomotion actions keep the legacy heuristic: their movement is still
    // animation root motion, so their reachability and their execution still
    // agree.
    timeNeededToGetToBall_ms = std::max(
        ballPredictionSize_ms,
        (unsigned int)(std::round(
            (match->GetBall()->Predict(ballPredictionSize_ms - 10).Get2D() -
             (GetPosition() + GetMovement() * 0.2f))
                .GetLength() /
            (GetMaxVelocity() * 0.75f) * 1000)));
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
    unsigned int startTime_ms = 0;
    if ((action_type == e_FunctionType_ShortPass ||
         action_type == e_FunctionType_LongPass ||
         action_type == e_FunctionType_HighPass ||
         action_type == e_FunctionType_Shot) &&
        !TouchPending()) {
      DO_VALIDATION;
      startTime_ms = 500;
    }

    bool refine = false;
    unsigned int timeStep_ms = 10;
    unsigned int previous_ms = 0;
    bool precise = (team->GetDesignatedTeamPossessionPlayer() == this) ? true : false;
    for (unsigned int ms = startTime_ms; ms < ballPredictionSize_ms;
         ms += timeStep_ms) {
      DO_VALIDATION;
      if (match->GetBall()->Predict(ms).coords[2] < 1.5f) {
        DO_VALIDATION;
        TimeNeeded result = AI_GetTimeNeededForDistance_ms(
            GetPosition(), GetMovement(), match->GetBall()->Predict(ms).Get2D(),
            GetMaxVelocity(), precise, ms);
        unsigned int timeNeeded = result.usual_ms;
        unsigned int timeNeeded_optimistic = result.optimistic_ms;

        if (timeNeeded_optimistic <= ms) {
          DO_VALIDATION;
          if (ms < timeNeededToGetToBall_optimistic_ms) {
            timeNeededToGetToBall_optimistic_ms = ms;
          }
        }

        if (timeNeeded <= ms) {
          DO_VALIDATION;

          // refinement round!
          if (!refine) {
            DO_VALIDATION;

            ms = previous_ms;
            timeStep_ms = 10;
            refine = true;
            // found!
          } else {
            timeNeededToGetToBall_ms = ms;
            break;
          }
        }
      }

      // refine timestep (optimisation)
      if (!refine) {
        DO_VALIDATION;
        float balldist = (GetPosition() - match->GetBall()->Predict(ms).Get2D()).GetLength() + 0.2f; // add a little buffer
        float maxBallVelo = 50;
        // how long does it take for the ball at max velo to travel balldist?
        unsigned int timeToGo_ms =
            int(std::round((balldist / maxBallVelo) * 1000.0f));
        timeStep_ms = clamp(timeToGo_ms, 10, 500);
        // round to 10s
        timeStep_ms = (timeStep_ms / 10) * 10;
      } else
        timeStep_ms = 10;

      previous_ms = ms;
    }
  }

  if (TouchAnim() && TouchPending()) {
    DO_VALIDATION;
    unsigned int animTimeToBall_ms = (GetTouchFrame() - GetCurrentFrame()) * 10;
    timeNeededToGetToBall_ms = std::min(timeNeededToGetToBall_ms, animTimeToBall_ms);
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
  }

  if (timeNeededToGetToBall_ms < defaultTouchOffset_ms) {
    DO_VALIDATION;
    // apply quantum mechanics on the scale of the very small ;)
    timeNeededToGetToBall_ms = NormalizedClamp(((GetPosition() + GetMovement() * (defaultTouchOffset_ms * 0.001)) - match->GetBall()->Predict(defaultTouchOffset_ms).Get2D()).GetLength(), 0.0f, 0.6f) * defaultTouchOffset_ms;
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
  }

  if ((action_type == e_FunctionType_ShortPass ||
       action_type == e_FunctionType_LongPass ||
       action_type == e_FunctionType_HighPass ||
       action_type == e_FunctionType_Shot) &&
      !TouchPending()) {
    DO_VALIDATION;
    hasPossession = false;
  } else {
    hasPossession = AI_HasPossession(match->GetBall(), this);
  }

  this->hasBestPossession = hasPossession && match->GetTeam(abs(team->GetID() - 1))->GetTimeNeededToGetToBall_ms() > this->GetTimeNeededToGetToBall_ms();
  this->hasUniquePossession = hasPossession && !match->GetTeam(abs(team->GetID() - 1))->HasPossession();

  if (match->GetBallRetainer() == this) {
    DO_VALIDATION;
    timeNeededToGetToBall_ms = 1;
    timeNeededToGetToBall_optimistic_ms = 1;
    SetDesiredTimeToBall_ms(timeNeededToGetToBall_ms);
    hasPossession = true;
    hasBestPossession = true;
    hasUniquePossession = true;
  } else if (match->GetBallRetainer() != 0) {
    DO_VALIDATION;
    hasPossession = false;
    hasBestPossession = false;
    hasUniquePossession = false;
  }
}

float Player::GetClosestOpponentDistance() const {
  Player *opp = AI_GetClosestPlayer(match->GetTeam(abs(team->GetID() - 1)), GetPosition(), false);
  return opp->GetPosition().GetDistance(GetPosition());
}

void Player::Process() {
  DO_VALIDATION;

  if (isActive) {
    DO_VALIDATION;

    desiredTimeToBall_ms = std::max(desiredTimeToBall_ms - 10, 0);

    if (ExternalControllerActive()) externalController->GetHumanController()->Process();
    CastController()->Process();

    if (match->IsInPlay()) {
      DO_VALIDATION;
      if (match->GetActualTime_ms() % 1000 == 0) {
        DO_VALIDATION;
        positionHistoryPerSecond.push_back(GetPosition());
        DO_VALIDATION;
      }
      DO_VALIDATION;
      if (hasPossession) possessionDuration_ms += 10; else possessionDuration_ms = 0;
      if ((match->GetActualTime_ms() + GetStableID() * 10) % 100 == 0) {
        DO_VALIDATION;
        _CalculateTacticalSituation();
      }
    }

    Vector3 posBefore = CastHumanoid()->GetPosition();

    CastHumanoid()->Process();
    SynchronizeKinematicState();
    CheckSimulationActionOracle();

    if (match->IsInPlay()) {
      Vector3 posAfter = CastHumanoid()->GetPosition();
      float distance = (posAfter - posBefore).GetLength();
      fatigueFactorInv -= distance * 0.00003f * (2.0f - GetStaminaStat()) * (1.0f / match->GetMatchDurationFactor());
      fatigueFactorInv = clamp(fatigueFactorInv, 0.01f, 1.0f);
    }
    // Don't send off the last player on the team.
    if (cards > 1 && cardEffectiveTime_ms <= match->GetActualTime_ms() &&
        GetTeam()->GetActivePlayersCount() > 1) {
      DO_VALIDATION;
      SendOff();
    }
  }
}


void Player::Put2D(bool /*mirror*/) {
  DO_VALIDATION;
}

void Player::Hide2D() {
  DO_VALIDATION;
}

void Player::SendOff() {
  DO_VALIDATION;
  // The deterministic RNG draw is deliberately kept: the baseline depends on
  // this consumption. The message it used to select is gone, so removing the
  // draw would be a gameplay change rather than a cleanup.
  (void)boostrandom(0, 3);

  Deactivate();

  if (GetFormationEntry().role == e_PlayerRole_GK) {
    DO_VALIDATION;
    FormationEntry entry = GetFormationEntry();
    std::vector<Player*> activePlayers;
    team->GetActivePlayers(activePlayers);
    assert(activePlayers.size() > 0);
    team->SetFormationEntry(*activePlayers.begin(), entry);
  }
  std::vector<Player*> activePlayers;
  team->GetActivePlayers(activePlayers);
}

float Player::GetStaminaStat() const {
  return playerData->GetStat(physical_stamina);
}

float Player::GetStat(PlayerStat name) const {
  float multiplier = 0.3f + 0.7f * team->GetAiDifficulty();
  multiplier *= 0.7f + 0.3f * GetFatigueFactorInv();

  return playerData->GetStat(name) * multiplier;
}

void Player::ProcessState(EnvState *state) {
  DO_VALIDATION;
  ProcessStateBase(state);
  state->process(manMarking);
  dynamicFormationEntry.ProcessState(state);
  state->process(hasPossession);
  state->process(hasBestPossession);
  state->process(hasUniquePossession);
  state->process(possessionDuration_ms);
  state->process(timeNeededToGetToBall_ms);
  state->process(timeNeededToGetToBall_optimistic_ms);
  state->process(timeNeededToGetToBall_previous_ms);
  state->process(triggerControlledBallCollision);
  tacticalSituation.ProcessState(state);
  state->process(desiredTimeToBall_ms);
  state->process(cards);
  state->process(cardEffectiveTime_ms);
}

void Player::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  PlayerBase::ResetSituation(focusPos);

  hasPossession = false;
  hasBestPossession = false;
  hasUniquePossession = false;
  possessionDuration_ms = 0;
  timeNeededToGetToBall_ms = 1000;
  timeNeededToGetToBall_optimistic_ms = 1000;
  SetDesiredTimeToBall_ms(0);
  manMarking = 0;

  triggerControlledBallCollision = false;

  tacticalSituation.forwardSpaceRating = 0;
  tacticalSituation.toGoalSpaceRating = 0;
  tacticalSituation.spaceRating = 0;
}

void Player::_CalculateTacticalSituation() {
  DO_VALIDATION;
  const MentalImage *mentalImage = static_cast<PlayerController*>(GetController())->GetMentalImage();
  assert(mentalImage);
  assert(IsActive());

  // calculate how free the path forward is
  float time_sec = 0.5f;
  Vector3 checkPos = GetPosition() + Vector3(-team->GetDynamicSide(), 0, 0) *
                                         sprintVelocity * time_sec;
  tacticalSituation.forwardSpaceRating = AI_CalculateFreeSpace(match, mentalImage, team->GetID(), checkPos, 5.0f, time_sec); // FREESPACE :D :D

  // calculate the amount of space this player has
  //tacticalSituation.spaceRating = AI_CalculatePersonalFreeSpace(match, mentalImage, this, 8.0f, 8.0f, 0.2f);
  time_sec = 0.1f;
  checkPos = GetPosition() + GetMovement() * time_sec;
  tacticalSituation.spaceRating = AI_CalculateFreeSpace(match, mentalImage, team->GetID(), checkPos, 5.0f, time_sec); // FREESPACE :D :D

  // distance to opponent goal 0 .. 1 == farthest .. closest
  tacticalSituation.forwardRating =
      1.0f - clamp((Vector3(pitchHalfW * -team->GetDynamicSide(), 0, 0) -
                    GetPosition())
                           .GetLength() /
                       (pitchHalfW * 2.0f),
                   0.0f, 1.0f);
  tacticalSituation.forwardRating =
      std::pow(tacticalSituation.forwardRating,
               1.5f);  // more important when close to goal
}
