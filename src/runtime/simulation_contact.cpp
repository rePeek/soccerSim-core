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

#include "runtime/player_contact.hpp"
#include "runtime/team_state.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

#include "football/ball/ball.hpp"
#include "player/player.hpp"
#include "football/contact/player_action_volume.hpp"
#include "football/contact/player_contact_kernel.hpp"
#include "football/contact/contact_evidence.hpp"

namespace football::sim {
namespace {

using namespace football::sim::player;

// How close the ball must be to the offender's contact for the challenge to
// count as an attempt to play the ball (Law 12 double jeopardy).
constexpr float kAttemptedPlayReach = 3.0f;


struct PlayerBounce {
  Player *opp;
  float force = 0.0f;
};

// Translate the active animation action into the rule-facing challenge family.
// This is the only place that reads the animation enum for a contact, so the
// Referee judges a named rule value instead of an animation type.
ChallengeAction ClassifyChallengeAction(e_FunctionType type) {
  if (type == e_FunctionType_Sliding) return ChallengeAction::SlidingChallenge;
  if (type == e_FunctionType_Interfere) return ChallengeAction::StandingChallenge;
  return ChallengeAction::Other;
}

// Freeze every rule-relevant contact input at the contact instant. The referee
// decides from this evidence instead of re-reading later actor state.
void FillContactEvidence(FoulAssessment& fact, Player* victim, Player* offender,
                         const Vector3& ball_position, Tick now,
                         const TeamMatchState* victim_team_state) {
  const PlayerActionState& action = offender->GetSimulationActionState();
  fact.offender = offender->GetID();
  fact.victim = victim->GetID();
  fact.victim_team_id = victim->GetTeamID();
  fact.offender_team_id = offender->GetTeamID();
  fact.contacted_at = now;
  fact.position = victim->GetPitchPosition();
  fact.victim_position = victim->GetPosition();
  fact.victim_direction = victim->GetDirectionVec();
  fact.offender_position = offender->GetPosition();
  fact.ball_position = ball_position;
  fact.action = ClassifyChallengeAction(action.type);
  fact.offender_scheduled_contact = action.HasScheduledContact();
  fact.offender_contact_frame = action.ContactFrame();
  fact.offender_frame = action.Frame();
  fact.offender_contact_position = action.contactPosition;
  fact.offender_last_touch_tick = offender->GetLastTouchTick();
  fact.victim_team_fading_possession =
      victim_team_state != nullptr ? victim_team_state->fading_possession_amount
                                   : 1.0f;
  // The offender genuinely challenged for the ball only when a challenge
  // animation was active and the ball was within reach at the contact.
  const football::math::Vector3 to_ball =
      ball_position.Get2D() - offender->GetPosition().Get2D();
  fact.attempted_to_play_ball =
      (action.type == e_FunctionType_Sliding || action.type == e_FunctionType_Interfere ||
       action.type == e_FunctionType_Trap || action.type == e_FunctionType_BallControl) &&
      to_ball.GetLength() < kAttemptedPlayReach;
}


FoulAssessment MakeSlidingAssessment(Player* victim, Player* tackler,
                                     const Vector3& ball_position, Tick now,
                                     const TeamMatchState* victim_team_state) {
  FoulAssessment fact;
  fact.kind = FoulKind::SlidingTackle;
  FillContactEvidence(fact, victim, tackler, ball_position, now, victim_team_state);
  fact.severity = football::contact::SlidingChallengeSeverity(fact);
  // Informational normalized score; the verdict uses the raw severity above.
  fact.score = NormalizedClamp(fact.severity, 1.0f, 2.0f);
  return fact;
}
ContactActorSnapshot CaptureContactActor(const Player& player,
                                        const PlayerContactInputs& inputs) {
  ContactActorSnapshot snapshot;
  snapshot.id = player.GetID();
  snapshot.team_id = player.GetTeamID();
  snapshot.position = player.GetPosition();
  snapshot.collider = player.GetGroundCollider();
  const PlayerKinematicState& kinematics = player.GetKinematicState();
  snapshot.velocity = kinematics.velocity;
  snapshot.facing = kinematics.GetFacing();
  snapshot.direction_vec = player.GetDirectionVec();
  snapshot.relative_body_angle = player.GetRelBodyAngle();
  snapshot.float_velocity = player.GetFloatVelocity();
  snapshot.action = player.GetSimulationActionState();
  snapshot.technical_standingtackle = player.GetStat(
      football::player::PlayerStat::technical_standingtackle);
  snapshot.technical_slidingtackle = player.GetStat(
      football::player::PlayerStat::technical_slidingtackle);
  snapshot.physical_balance =
      player.GetStat(football::player::PlayerStat::physical_balance);
  snapshot.decaying_position_offset = player.GetDecayingPositionOffset();
  const football::player::PlayerBallRelation& relation =
      inputs.ball_table->Read(player.GetTeamID(), &player);
  snapshot.has_possession = relation.locally_controllable;
  snapshot.has_best_possession = relation.best_in_team;
  snapshot.global_designated_possession =
      &player == inputs.designated_possession_player;
  const TeamMatchState* team_state = inputs.team_states[player.GetTeamID()];
  snapshot.team_designated_possession =
      team_state != nullptr && team_state->designated_actor == &player;
  return snapshot;
}

void ResolvePlayerPair(Player *p1, Player *p2,
                       std::vector<PlayerBounce> &p1Bounce,
                       std::vector<PlayerBounce> &p2Bounce,
                       const PlayerContactInputs& inputs,
                       std::vector<FoulAssessment>* assessments) {
  ContactPairEnvironment environment;
  environment.ball_next_position =
      inputs.ball.Predict(football::sim::TickSpan{1}).position.Get2D();

  const ContactActorSnapshot first = CaptureContactActor(*p1, inputs);
  const ContactActorSnapshot second = CaptureContactActor(*p2, inputs);
  const ContactPairResult resolved =
      football::sim::player::ResolveContactPair(first, second, environment);

  if (resolved.first_has_offset) p1->OffsetPosition(resolved.first_offset);
  if (resolved.second_has_offset) p2->OffsetPosition(resolved.second_offset);

  if (resolved.first_similar)
    p1Bounce.push_back({p2, resolved.first_similar_force});
  if (resolved.second_similar)
    p2Bounce.push_back({p1, resolved.second_similar_force});

  if (resolved.trip_range && assessments != nullptr) {
    if (auto assessment = AssessCollision(
            p1, p2, resolved.first_sensitivity, resolved.second_sensitivity,
            inputs.ball, inputs.now, inputs.team_states)) {
      assessments->push_back(*assessment);
    }
  }

  if (resolved.first_trip.active) {
    p1->React(PlayerReaction{ReactionKind::Trip,
                             resolved.first_trip.direction,
                             resolved.first_trip.severity, p2->GetID(),
                             resolved.first_trip.trip_type},
              inputs.ball_retainer);
  }
  if (resolved.second_trip.active) {
    p2->React(PlayerReaction{ReactionKind::Trip,
                             resolved.second_trip.direction,
                             resolved.second_trip.severity, p1->GetID(),
                             resolved.second_trip.trip_type},
              inputs.ball_retainer);
  }

  // Sample tackle geometry from the post-offset actors, preserving pair order.
  const PlayerActionState &p1Action = p1->GetSimulationActionState();
  const PlayerActionState &p2Action = p2->GetSimulationActionState();
  const PlayerActionVolume p1Tackle =
      BuildTackleVolume(p1Action, p1->GetKinematicState());
  const PlayerActionVolume p2Tackle =
      BuildTackleVolume(p2Action, p2->GetKinematicState());
  int tackle = 0;
  if (p1Tackle.active) tackle += 1;
  if (p2Tackle.active) tackle += 2;
  if (resolved.distance < 2.0f && tackle > 0 && tackle < 3) {
    const PlayerActionVolume &tacklerVolume =
        tackle == 1 ? p1Tackle : p2Tackle;
    const PlayerActionState &tacklerAction =
        tackle == 1 ? p1Action : p2Action;
    Player *tackler = tackle == 1 ? p1 : p2;
    Player *victim = tackle == 1 ? p2 : p1;

    if (tacklerVolume.Intersects(victim->GetGroundCollider())) {
      if (tacklerAction.Frame() > 10 &&
          tacklerAction.Frame() < tacklerAction.FrameCount() - 6) {
        Vector3 tripVec = victim->GetKinematicState().GetFacing();
        int tripType = 3;
        if (tacklerAction.type == e_FunctionType_Interfere) tripType = 1;
        victim->React(PlayerReaction{ReactionKind::Trip, tripVec, 1.0f,
                                     tackler->GetID(), tripType},
                      inputs.ball_retainer);
        if (tripType == 3 && assessments != nullptr) {
          assessments->push_back(MakeSlidingAssessment(
              victim, tackler, inputs.ball.state().position, inputs.now,
              inputs.team_states[victim->GetTeamID()]));
        }
      }
    }
  }
}

}  // namespace

std::optional<FoulAssessment> AssessCollision(Player* first, Player* second,
                                              float first_sensitivity,
                                              float second_sensitivity,
                                              const football::ball::Ball& ball,
                                              Tick now,
                                              const TeamMatchState* const* team_states) {
  if (first == nullptr || second == nullptr) return std::nullopt;
  const auto attribution = football::contact::AttributeStandingFall(
      {first->GetID(), first->GetTeamID(), first_sensitivity},
      {second->GetID(), second->GetTeamID(), second_sensitivity});
  if (!attribution) return std::nullopt;
  Player* victim = attribution->victim_is_first ? first : second;
  Player* offender = victim == first ? second : first;

  FoulAssessment assessment;
  assessment.kind = FoulKind::StandingFall;
  FillContactEvidence(assessment, victim, offender, ball.state().position, now,
                      team_states[victim->GetTeamID()]);
  assessment.score = attribution->score;
  return assessment;
}

void ResolvePlayerContacts(const PlayerContactInputs& inputs,
                           std::vector<FoulAssessment>& assessments) {
  const auto players = inputs.players;
  // Avoid unsigned underflow for an empty explicit span; normal rosters are nonempty.
  if (players.size() < 2) return;

  // outer vectors index == players[] index
  std::vector < std::vector<PlayerBounce> > playerBounces;

  // insert an empty entry for every player
  playerBounces.resize(players.size());

  // check each combination of humanoids once
  for (unsigned int i1 = 0; i1 < players.size() - 1; i1++) {
    for (unsigned int i2 = i1 + 1; i2 < players.size(); i2++) {
      ResolvePlayerPair(players[i1], players[i2], playerBounces.at(i1),
                       playerBounces.at(i2), inputs, &assessments);
    }
  }

  // do bouncy magic
  for (unsigned int i1 = 0; i1 < players.size(); i1++) {

    float totalForce = 0.0f;

    for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {

      const PlayerBounce &bounce = playerBounces.at(i1).at(i2);
      totalForce += bounce.force;
    }

    if (totalForce > 0.0f) {

      Vector3 bounceVec;
      for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {

        const PlayerBounce &bounce = playerBounces.at(i1).at(i2);
        bounceVec += (bounce.opp->GetMovement() - players[i1]->GetMovement()) * bounce.force * (bounce.force / totalForce);
      }

      // okay, accumulated all, now distribute them in normalized fashion
      players[i1]->OffsetPosition(bounceVec * 0.01f * 1.0f);
    }
  }
}

}  // namespace football::sim
