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

#include "sim/player/player_contact.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

#include "football/ball/ball.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_action_volume.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/fact/simulation_fact_sink.hpp"
#include "sim/team/team.hpp"

namespace football::sim {
namespace {

struct PlayerBounce {
  Player *opp;
  float force = 0.0f;
};

// Freeze every rule-relevant input at the contact instant. The referee decides
// from this evidence instead of re-reading later actor state.
football::sim::event::PlayerTripFact MakeTripFact(Player* victim, Player* offender,
                                                  int trip_type,
                                                  const Vector3& ball_position) {
  const PlayerActionState& action = offender->GetSimulationActionState();
  football::sim::event::PlayerTripFact fact;
  fact.victim = victim->GetID();
  fact.offender = offender->GetID();
  fact.victim_team_id = victim->GetTeam()->GetID();
  fact.offender_team_id = offender->GetTeam()->GetID();
  fact.trip_type = trip_type;
  fact.victim_position = victim->GetPosition();
  fact.victim_pitch_position = victim->GetPitchPosition();
  fact.victim_direction = victim->GetDirectionVec();
  fact.offender_position = offender->GetPosition();
  fact.ball_position = ball_position;
  fact.offender_action_type = static_cast<int>(action.type);
  fact.offender_scheduled_contact = action.HasScheduledContact();
  fact.offender_contact_frame = action.ContactFrame();
  fact.offender_frame = action.Frame();
  fact.offender_contact_position = action.contactPosition;
  fact.offender_last_touch_tick = offender->GetLastTouchTick();
  fact.victim_team_fading_possession =
      victim->GetTeam()->GetFadingTeamPossessionAmount();
  return fact;
}

void ResolvePlayerPair(Player *p1, Player *p2,
                       std::vector<PlayerBounce> &p1Bounce,
                       std::vector<PlayerBounce> &p2Bounce,
                       const PlayerContactInputs& inputs, SimulationFactSink& facts) {
  constexpr float distanceFactor = 0.72f;
  constexpr float bouncePlayerRadius = 0.5f * distanceFactor;
  constexpr float similarPlayerRadius = 0.8f * distanceFactor;
  constexpr float similarExp = 0.2f;//0.8f;
  constexpr float similarForceFactor = 0.25f; // 0.5f would be the full effect

  const PlayerGroundCollider &p1Collider = p1->GetGroundCollider();
  const PlayerGroundCollider &p2Collider = p2->GetGroundCollider();
  Vector3 p1pos = p1Collider.center;
  Vector3 p2pos = p2Collider.center;
  float distance = (p1pos - p2pos).GetLength();

  Vector3 p1movement = p1->GetKinematicState().velocity;
  Vector3 p2movement = p2->GetKinematicState().velocity;
  assert(p1movement.coords[2] == 0.0f);
  assert(p2movement.coords[2] == 0.0f);

  float bounceBias = 0.0f;
  Vector3 bounceVec;
  float p1backFacing = 0.5f;
  float p2backFacing = 0.5f;

  if (distance < bouncePlayerRadius * 2.0f ||
      distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {

    bounceVec = (p1pos - p2pos).GetNormalized(Vector3(0, -1, 0));

    // back facing
    Vector3 p1facing = p1->GetKinematicState().facing.GetRotated2D(p1->GetRelBodyAngle() * 0.7f);
    Vector3 p2facing = p2->GetKinematicState().facing.GetRotated2D(p2->GetRelBodyAngle() * 0.7f);
    p1backFacing = clamp(p1facing.GetDotProduct( bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f); // 0 .. 1 == worst .. best
    p2backFacing = clamp(p2facing.GetDotProduct(-bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f);

    if (p1->GetGroundCollider().Intersects(p2->GetGroundCollider())) {

      bounceBias += p1backFacing * 0.8f;
      bounceBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      const PlayerActionState &p1Action = p1->GetSimulationActionState();
      const PlayerActionState &p2Action = p2->GetSimulationActionState();
      bounceBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1Action.IsContactPending() && p1Action.type == e_FunctionType_Interfere) bounceBias += 0.1f + 0.4f * p1->GetStat(football::model::PlayerStat::technical_standingtackle);
      if (p1Action.IsContactPending() && p1Action.type == e_FunctionType_Sliding)   bounceBias += 0.1f + 0.4f * p1->GetStat(football::model::PlayerStat::technical_slidingtackle);
      if (p2Action.IsContactPending() && p2Action.type == e_FunctionType_Interfere) bounceBias -= 0.1f + 0.4f * p2->GetStat(football::model::PlayerStat::technical_standingtackle);
      if (p2Action.IsContactPending() && p2Action.type == e_FunctionType_Sliding)   bounceBias -= 0.1f + 0.4f * p2->GetStat(football::model::PlayerStat::technical_slidingtackle);

      // problem is, once possession is lost (usually directly after ball is touched), bias may turn around the other way. (well, maybe that's not a problem. dunno.)
      // if (p1->HasPossession() == true) bounceBias -= 0.3f;
      // if (p2->HasPossession() == true) bounceBias += 0.3f;

      if (p1 == inputs.designated_possession_player) bounceBias += 0.4f;
      if (p2 == inputs.designated_possession_player) bounceBias -= 0.4f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (inputs.ball.Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (inputs.ball.Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor = clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) * 1.0f; // std::min is cap so difference won't matter if ball is far away (so only used in battles about the ball)
        bounceBias += ballDistanceDiffFactor;
      }

      bounceBias += p1->GetStat(football::model::PlayerStat::physical_balance) * 1.0f;
      bounceBias -= p2->GetStat(football::model::PlayerStat::physical_balance) * 1.0f;

      bounceBias = clamp(bounceBias, -1.0f, 1.0f);
      bounceBias *= 0.5f;

      // convert bounceBias to 0 .. 1 instead of -1 .. 1
      float bounceBias0to1 = bounceBias * 0.5f + 0.5f;
      //bounceBias0to1 = curve(bounceBias0to1, 0.5f); // more binary

      Vector3 offset1 = (p1pos - p2pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) * (1.0f - bounceBias0to1) * 2.0f;
      Vector3 offset2 = (p2pos - p1pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) * bounceBias0to1 * 2.0f;

      // slow down on contact
      /*
      Vector3 averageMomentum = (p1movement + p2movement) * 0.5f;
      offset1 -= averageMomentum * 0.001f;
      offset2 -= averageMomentum * 0.001f;
      */

      // make players snap to the side of opponents (rather, just a bit in front of them too)


      if (inputs.designated_possession_player == p2 && p2->HasPossession()) {
        Vector3 p2_leftside = p2pos + p2->GetKinematicState().facing.GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p2_rightside = p2pos + p2->GetKinematicState().facing.GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p1_to_p2_left = (p1pos - p2_leftside).GetLength();
        float p1_to_p2_right = (p1pos - p2_rightside).GetLength();
        Vector3 p2side = p1_to_p2_left < p1_to_p2_right ? p2_leftside : p2_rightside;
        // SetYellowDebugPilon(p2side);
        offset1 += (p2side - p1pos).GetNormalizedMax(0.01f) * p1->GetStat(football::model::PlayerStat::physical_balance) * 0.3f;
      }

      else if (inputs.designated_possession_player == p1 && p1->HasPossession()) {
        Vector3 p1_leftside = p1pos + p1->GetKinematicState().facing.GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p1_rightside = p1pos + p1->GetKinematicState().facing.GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p2_to_p1_left = (p2pos - p1_leftside).GetLength();
        float p2_to_p1_right = (p2pos - p1_rightside).GetLength();
        Vector3 p1side = p2_to_p1_left < p2_to_p1_right ? p1_leftside : p1_rightside;
        // SetRedDebugPilon(p1side);
        offset2 += (p1side - p2pos).GetNormalizedMax(0.01f) * p2->GetStat(football::model::PlayerStat::physical_balance) * 0.3f;
      }

      // can not bump faster than sprint
      offset1.NormalizeMax(sprintVelocity * 0.01f);
      offset2.NormalizeMax(sprintVelocity * 0.01f);


      p1->OffsetPosition(offset1);
      p2->OffsetPosition(offset2);
    }

    // take over each others movement a bit (precalc phase)

    float similarBias = 0.0f;

    if (similarForceFactor > 0.0f &&
        distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
      float shellDistance = std::max(0.0f, distance - bouncePlayerRadius * 2.0f);

      similarBias += p1backFacing * 0.8f;
      similarBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      similarBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1 == inputs.designated_possession_player) similarBias += 0.6f;
      if (p2 == inputs.designated_possession_player) similarBias -= 0.6f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (inputs.ball.Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (inputs.ball.Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor = clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) * 1.0f; // std::min is cap so difference won't matter if ball is far away (so only used in battles about the ball)
        similarBias += ballDistanceDiffFactor;
      }

      similarBias += p1->GetStat(football::model::PlayerStat::physical_balance) * 1.0f;
      similarBias -= p2->GetStat(football::model::PlayerStat::physical_balance) * 1.0f;

      similarBias = clamp(similarBias, -1.0f, 1.0f);
      similarBias *= 0.9f;

      float similarForce = clamp(1.0f - (shellDistance / (similarPlayerRadius * 2.0f)), 0.0f, 1.0f);
      similarForce = std::pow(similarForce, similarExp);
      similarForce *= similarForceFactor;

      assert(similarForce >= 0.0f && similarForce <= 1.0f);

      float similarBias0to1 = similarBias * 0.5f + 0.5f;


      PlayerBounce player1Bounce;
      player1Bounce.opp = p2;
      player1Bounce.force = similarForce * (1.0f - similarBias0to1);
      p1Bounce.push_back(player1Bounce);

      PlayerBounce player2Bounce;
      player2Bounce.opp = p1;
      player2Bounce.force = similarForce * similarBias0to1;
      p2Bounce.push_back(player2Bounce);
    }

    // u b trippin?

    if (distance < bouncePlayerRadius * 2.0f) {

      float p1sensitivity = 0.0f;
      float p2sensitivity = 0.0f;

      p1sensitivity += (1.0f - p1backFacing) * 1.0f;
      p2sensitivity += (1.0f - p2backFacing) * 1.0f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      p1sensitivity += NormalizedClamp(p1velocity, idleVelocity, sprintVelocity) * 1.0f;
      p2sensitivity += NormalizedClamp(p2velocity, idleVelocity, sprintVelocity) * 1.0f;

      if (p1->HasBestPossession() == true) p1sensitivity += 1.0f;
      if (p2->HasBestPossession() == true) p2sensitivity += 1.0f;

      float balanceWeight = 3.0f;
      p1sensitivity += (1.0f - p1->GetStat(football::model::PlayerStat::physical_balance) * 1.0f) * balanceWeight;
      p2sensitivity += (1.0f - p2->GetStat(football::model::PlayerStat::physical_balance) * 1.0f) * balanceWeight;

      p1sensitivity += clamp(p1->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);
      p2sensitivity += clamp(p2->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);

      // penetration
      float penetrationWeight = 6.0f;
      float penetration = ( (p1->GetPosition() + p1->GetKinematicState().velocity * 0.03f) - (p2->GetPosition() + p2->GetKinematicState().velocity * 0.03f) ).GetLength();
      //if (p1->GetDebug() || p2->GetDebug()) printf("penetration: %f\n", pow(1.0f - NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f));
      p1sensitivity +=
          std::pow(1.0f - NormalizedClamp(penetration, 0.0f,
                                          bouncePlayerRadius * 2.0f),
                   0.4f) *
          penetrationWeight;
      p2sensitivity +=
          std::pow(1.0f - NormalizedClamp(penetration, 0.0f,
                                          bouncePlayerRadius * 2.0f),
                   0.4f) *
          penetrationWeight;

      // ball proximity (usually means: stability is less because we sacrifice balance to control the ball)
      float p1BallDistance = (inputs.ball.Predict(10).Get2D() - p1->GetPosition()).GetLength();
      float p2BallDistance = (inputs.ball.Predict(10).Get2D() - p2->GetPosition()).GetLength();
      p1sensitivity += 1.0f - NormalizedClamp(p1BallDistance, 0.0f, 0.7f);
      p2sensitivity += 1.0f - NormalizedClamp(p2BallDistance, 0.0f, 0.7f);

      // divided by elements active
      p1sensitivity /= 5.0f + balanceWeight + penetrationWeight;
      p2sensitivity /= 5.0f + balanceWeight + penetrationWeight;

      float trip0threshold = 0.38f;
      float trip1threshold = 0.48f;
      float trip2threshold = 0.58f;

      if (p1sensitivity > trip0threshold) {
        int tripType = 0;
        if (p1sensitivity > trip1threshold) tripType = 1;
        if (p1sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          p1->TripMe((p1->GetKinematicState().velocity * 0.1f + p2->GetKinematicState().velocity * 0.06f + bounceVec * 1.0f).GetNormalized(bounceVec), tripType, inputs.ball_retainer);
          facts.OnSimulationFact(MakeTripFact(p1, p2, tripType, inputs.ball.Predict(0)));
        }
      }
      if (p2sensitivity > trip0threshold) {
        int tripType = 0;
        if (p2sensitivity > trip1threshold) tripType = 1;
        if (p2sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          p2->TripMe((p2->GetKinematicState().velocity * 0.1f + p1->GetKinematicState().velocity * 0.06f - bounceVec * 1.0f).GetNormalized(-bounceVec), tripType, inputs.ball_retainer);
          facts.OnSimulationFact(MakeTripFact(p2, p1, tripType, inputs.ball.Predict(0)));
        }
      }

    }  // within either bump, similar or trip range
  }

  // check for tackling collisions

  // The tackle contact test is a simulation-owned action volume (a forward
  // capsule) against the victim's ground collider. It replaces the legacy
  // per-body-part geometry AABB test, so no scene object is involved.
  const PlayerActionState &p1Action = p1->GetSimulationActionState();
  const PlayerActionState &p2Action = p2->GetSimulationActionState();
  const PlayerActionVolume p1Tackle =
      BuildTackleVolume(p1Action, p1->GetKinematicState());
  const PlayerActionVolume p2Tackle =
      BuildTackleVolume(p2Action, p2->GetKinematicState());
  int tackle = 0;
  if (p1Tackle.active) tackle += 1;
  if (p2Tackle.active) tackle += 2;
  if (distance < 2.0f && tackle > 0 && tackle < 3) {
      // if tackle is 3, ignore both
    const PlayerActionVolume &tacklerVolume =
        tackle == 1 ? p1Tackle : p2Tackle;
    const PlayerActionState &tacklerAction =
        tackle == 1 ? p1Action : p2Action;
    Player *tackler = tackle == 1 ? p1 : p2;
    Player *victim = tackle == 1 ? p2 : p1;

    if (tacklerVolume.Intersects(victim->GetGroundCollider())) {
      if (tacklerAction.Frame() > 10 &&
          tacklerAction.Frame() < tacklerAction.FrameCount() - 6) {
        Vector3 tripVec = victim->GetKinematicState().facing;
        int tripType = 3;  // sliding
        if (tacklerAction.type == e_FunctionType_Interfere)
          tripType = 1;  // was 2
        victim->TripMe(tripVec, tripType, inputs.ball_retainer);
        facts.OnSimulationFact(MakeTripFact(victim, tackler, tripType, inputs.ball.Predict(0)));
      }
    }
  }
}

}  // namespace

void ResolvePlayerContacts(const PlayerContactInputs& inputs, SimulationFactSink& facts) {
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
      ResolvePlayerPair(players[i1], players[i2], playerBounces.at(i1), playerBounces.at(i2), inputs, facts);
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
