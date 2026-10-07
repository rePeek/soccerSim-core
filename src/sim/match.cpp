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

#include "sim/match.hpp"
#include "sim/animation/library.hpp"

#include "foundation/geometry/line.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "foundation/geometry/triangle.hpp"
#include <cassert>
#include "sim/player/player_action_volume.hpp"
#include "sim/player/player_body_collider.hpp"




const std::vector<Vector3> &Match::GetAnimPositionCache(
    AnimationId animation_id) const {
  return animations_->Get(static_cast<uint32_t>(animation_id))
      .root_positions;
}

Match::Match(const football::model::Team& home, const football::model::Team& away,
             const football::model::Pitch& pitch,
             const MatchOptions& options,
             SimulationRng& rng,
             std::shared_ptr<const AnimationLibrary> animation_library)
    : pitch_(pitch),
      animations_(std::move(animation_library)),
      rng_(rng),
      first_team(options.reverse_team_processing ? 1 : 0),
      second_team(options.reverse_team_processing ? 0 : 1),
      possessionSideHistory(6000),
      options_(options) {




  ball = new Ball(this);

  // The baked animation library is owned by the simulation and shared with
  // every match it creates; the runtime reads only that baked artifact.
  designatedPossessionPlayer = 0;


  // teams

  const football::model::Team* descriptions[] = {&home, &away};
  teams[first_team] =
      new Team(first_team, this, *descriptions[first_team],
               first_team ? options.right_team_difficulty
                          : options.left_team_difficulty);
  teams[second_team] =
      new Team(second_team, this, *descriptions[second_team],
               second_team ? options.right_team_difficulty
                           : options.left_team_difficulty);
  teams[first_team]->SetOpponent(teams[second_team]);
  teams[second_team]->SetOpponent(teams[first_team]);
  // Preserve the historical scheduling stagger across both rosters, including
  // reversed processing. Only a periodic phase is passed, not a creation ID.
  teams[first_team]->InitPlayers(0);
  teams[second_team]->InitPlayers(static_cast<std::uint8_t>(
      teams[first_team]->GetAllPlayers().size() % 10));

  std::vector<Player*> activePlayers;
  teams[first_team]->GetActivePlayers(activePlayers);
  designatedPossessionPlayer = activePlayers.at(0);
  ballRetainer = 0;


  // match params

  lastGoalTeam = 0;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeam = 0;
  SetMatchPhase(MatchPhase::PreMatch);

  // Football rules, independent of any referee/linesman humanoid actors.
  referee_ = std::make_unique<Referee>(this);




}

Match::~Match() {}


void Match::Mirror(bool team_0, bool team_1, bool ball) {
  if (team_0) {
    teams[0]->Mirror();
  }
  if (team_1) {
    teams[1]->Mirror();
  }
  if (ball) {
    ball_mirrored = !ball_mirrored;
    this->ball->Mirror();
  }
  for (auto &i : mentalImages) {
    i.Mirror(team_0, team_1, ball);
  }
}

void Match::Exit() {
  teams[first_team]->Exit();
  teams[second_team]->Exit();
  delete teams[first_team];
  delete teams[second_team];
  delete ball;
  referee_.reset();
  mentalImages.clear();


}


void Match::GetActiveTeamPlayers(int teamID, std::vector<Player *> &players) {
  teams[teamID]->GetActivePlayers(players);
}

MentalImage *Match::GetMentalImage(int history_ms) {
  int index = int(round((float)history_ms / 100.0));
  if (index >= (signed int)mentalImages.size()) index = mentalImages.size() - 1;
  if (index < 0) index = 0;
  return &mentalImages[index];
}

void Match::UpdateLatestMentalImageBallPredictions() {
  if (!mentalImages.empty()) mentalImages[0].UpdateBallPredictions();
}

void Match::ResetSituation(const Vector3 &focusPos) {
  ++reset_sequence_;
  SetBallRetainer(0);
  SetGoalScored(false);
  mentalImages.clear();
  goalScored = false;
  ballIsInGoal = false;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeam = 0;

  possessionSideHistory.Clear();

  last_body_ball_collision_tick_ = {};

  ball->ResetSituation(focusPos);
  teams[first_team]->ResetSituation(focusPos);
  teams[second_team]->ResetSituation(focusPos);
}

void Match::SwitchEnds() {
  // A permanent change of ends keeps the canonical between-tick frame: only the
  // stored actors, ball and memories move, and mirrored/ball_mirrored stay false.
  teams[first_team]->SwitchEnds();
  teams[second_team]->SwitchEnds();
  ball->Mirror();
  for (auto &i : mentalImages) {
    i.Mirror(true, true, true);
  }
}

void Match::SetMatchPhase(MatchPhase newMatchPhase) {
  matchPhase = newMatchPhase;
  if (Finished()) { EndHalf(); return; }
  teams[first_team]->RelaxFatigue(1.0f);
  teams[second_team]->RelaxFatigue(1.0f);
}

void Match::StartBallInPlay() {
  if (!IsInPlay() || (matchPhase != MatchPhase::FirstHalf &&
                      matchPhase != MatchPhase::SecondHalf))
    throw std::logic_error("ball cannot enter play outside an authorized half");
  regulation_running_ = true;
  ball_in_play_ = true;
}

MatchResult Match::Result() const {
  if (!Finished()) throw std::logic_error("match result requires full time");
  const auto outcome = score_[0] > score_[1] ? MatchOutcome::HomeWin
      : score_[0] < score_[1] ? MatchOutcome::AwayWin : MatchOutcome::Draw;
  return {score_[0], score_[1], outcome, duration_ticks_};
}

Team *Match::GetBestPossessionTeam() {
  return bestPossessionTeam;
}




// THE SPICE

bool Match::Step(const PlayerControlSet& controls) {
  if (Finished()) return false;
  if (pending_change_of_ends_) {
    SwitchEnds();
    pending_change_of_ends_ = false;
  }
  ++duration_ticks_;
  bool reverse = options_.reverse_team_processing;

  for (int team_id = 0; team_id < 2; ++team_id) {
    std::vector<Player*> players;
    teams[team_id]->GetAllPlayers(players);
    for (Player* player : players) {
      player->ClearControl();
      if (const PlayerControl* control =
              controls.Get(player->GetID())) {
        player->SetControl(*control);
      }
    }
  }


  Mirror(reverse, !reverse, reverse);
  // A clock that already elapsed must whistle before this tick can still collide
  // the ball. The referee stays the single period authority.
  if (IsBallInPlay() && !referee_->PeriodElapsed()) {
    CheckBallCollisions();
  }

  referee_->Process();
  Vector3 previousBallPos = ball->Predict(0);
  Mirror(reverse, !reverse, reverse);
  // Restore the processing frame even on the referee's terminal transition.
  if (Finished()) return false;
  if (!IsInPlay() && !referee_->RestartNeedsSimulation() &&
      (now_ < referee_->GetBuffer().prepare_tick ||
       referee_->GetBuffer().prepare_tick + football::sim::TickSpan{1} < now_)) {
    // Ceremonies execute only their placement tail; both clocks stay stopped.
    AdvanceTime(football::sim::TickSpan{1});
    return false;
  }
  Mirror(false, false, reverse);
  ball->Process();
  Mirror(false, false, reverse);

  // create mental images for the AI to use
  constexpr football::sim::TickSpan kMentalImageCadence{10};
  if (mentalImages.empty() || now_.value % kMentalImageCadence.value == 0) {
    mentalImages.insert(mentalImages.begin(), MentalImage(this));
    if (mentalImages.size() > 3) {
      mentalImages.pop_back();
    }
  }

  Mirror(first_team == 1, first_team == 0, first_team == 1);
  teams[first_team]->Process();
  Mirror(true, true, true);
  teams[second_team]->Process();
  Mirror(first_team == 0, first_team == 1, first_team == 0);

  Mirror(first_team == 1, first_team == 0, first_team == 1);
  teams[first_team]->UpdatePossessionStats();
  Mirror(true, true, true);
  teams[second_team]->UpdatePossessionStats();
  Mirror(first_team == 0, first_team == 1, first_team == 0);

  CalculateBestPossessionTeamID();

  if (GetBallRetainer() == 0) {
    if (GetBestPossessionTeam()) {
      Player *candidate = GetBestPossessionTeam()->GetDesignatedTeamPossessionPlayer();
      if (candidate != GetDesignatedPossessionPlayer()) {
        unsigned int designatedTime = GetDesignatedPossessionPlayer()->GetTimeNeededToGetToBall_ms();
        unsigned int candidateTime = candidate->GetTimeNeededToGetToBall_ms();
        float timeRating = (float)(candidateTime + 10) / (float)(designatedTime + 10);
        if (timeRating < 0.85f) designatedPossessionPlayer = candidate;
      }
    } else {
      // just stick with current team
      designatedPossessionPlayer = GetDesignatedPossessionPlayer()->GetTeam()->GetDesignatedTeamPossessionPlayer();
    }
  } else {
    designatedPossessionPlayer = GetBallRetainer();
  }

  Mirror(reverse, !reverse, reverse);
  CheckHumanoidCollisions();

  AdvanceTime(football::sim::TickSpan{1});

  // check for goals
  bool first_team_goal = false;
  bool second_team_goal = false;
  if (IsBallInPlay()) {
    first_team_goal =
        CheckForGoal(teams[first_team]->GetDynamicSide(), previousBallPos);
    second_team_goal =
        CheckForGoal(teams[second_team]->GetDynamicSide(), previousBallPos);
  }
  bool goal = first_team_goal | second_team_goal;
  ballIsInGoal |= goal;
  Mirror(reverse, !reverse, reverse);
  if (IsBallInPlay()) {
    if (goal) {
      int team = first_team_goal ? second_team : first_team;
      ++score_[teams[team]->GetID()];
      SetGoalScored(true);
      lastGoalTeam = teams[team];
    }
    if (first_team_goal || second_team_goal) {

      // find out who scored
      bool ownGoal = true;
      if (GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == GetLastGoalTeam()->GetID() || GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == GetLastGoalTeam()->GetID()) ownGoal = false;

      if (!ownGoal) {
        lastGoalScorer = GetLastGoalTeam()->GetLastTouchPlayer();
      }

      else {  // own goal
        lastGoalScorer = teams[abs(GetLastGoalTeam()->GetID() - 1)]->GetLastTouchPlayer();
      }
    }
  }
  // average possession side

   if (IsBallInPlay()) {
     if (GetBestPossessionTeam()) {
       float sideValue = 0;
       sideValue += (GetTeam(0)->GetFadingTeamPossessionAmount() - 0.5f) *
           GetTeam(0)->GetDynamicSide();
       sideValue += (GetTeam(1)->GetFadingTeamPossessionAmount() - 0.5f) *
           GetTeam(1)->GetDynamicSide();
       possessionSideHistory.Insert(sideValue);
     }
   }

  return true;
}

bool Match::CheckForGoal(signed int side, const Vector3 &previousBallPos) {
  if (fabs(ball->Predict(10).coords[0]) < pitch_.half_length() - 1.0) return false;

  Line line;
  line.SetVertex(0, previousBallPos);
  line.SetVertex(1, ball->Predict(0));

  const float goal_x =
      (pitch_.half_length() + pitch_.line_half_width() + 0.11f) * side;
  const float half_goal_width = pitch_.goal_half_width();
  const float goal_height = pitch_.goal_height();
  Triangle goal1;
  goal1.SetVertex(0, Vector3(goal_x, half_goal_width, 0));
  goal1.SetVertex(1, Vector3(goal_x, -half_goal_width, 0));
  goal1.SetVertex(2, Vector3(goal_x, half_goal_width, goal_height));
  goal1.SetNormals(Vector3(-side, 0, 0));
  Triangle goal2;
  goal2.SetVertex(0, Vector3(goal_x, -half_goal_width, 0));
  goal2.SetVertex(1, Vector3(goal_x, -half_goal_width, goal_height));
  goal2.SetVertex(2, Vector3(goal_x, half_goal_width, goal_height));
  goal2.SetNormals(Vector3(-side, 0, 0));

  Vector3 intersectVec;
  bool intersect1 = goal1.IntersectsLine(line, intersectVec);
  bool intersect2 = goal2.IntersectsLine(line, intersectVec);
  // extra check: ball could have gone 'in' via the side netting, if line begin
  // == inside pitch, but outside of post, and line end == in goal. disallow!
  if (fabs(previousBallPos.coords[1]) > 3.7 &&
      fabs(previousBallPos.coords[0]) >
          pitch_.half_length() - pitch_.line_half_width() - 0.11) {
    return false;
  }
  if (intersect1 || intersect2) {
    return true;
  }
  return false;
}

void Match::CalculateBestPossessionTeamID() {
  if (GetBallRetainer() != 0) {
    bestPossessionTeam = GetBallRetainer()->GetTeam();
  } else {
    int bestTime_ms[2] = { 100000, 100000 };
    bestTime_ms[0] = teams[first_team]->GetTimeNeededToGetToBall_ms();
    bestTime_ms[1] = teams[second_team]->GetTimeNeededToGetToBall_ms();
    if (bestTime_ms[0] < bestTime_ms[1])
      bestPossessionTeam = teams[first_team];
    else if (bestTime_ms[0] > bestTime_ms[1])
      bestPossessionTeam = teams[second_team];
    else {
      assert(bestTime_ms[0] == bestTime_ms[1]);
      bestPossessionTeam = 0;
    }
  }
}

void Match::CheckHumanoidCollisions() {
  std::vector<Player*> players;

  GetTeam(first_team)->GetActivePlayers(players);
  GetTeam(second_team)->GetActivePlayers(players);

  // outer vectors index == players[] index
  std::vector < std::vector<PlayerBounce> > playerBounces;

  // insert an empty entry for every player
  playerBounces.resize(players.size());

  // check each combination of humanoids once
  for (unsigned int i1 = 0; i1 < players.size() - 1; i1++) {
    for (unsigned int i2 = i1 + 1; i2 < players.size(); i2++) {
      CheckHumanoidCollision(players.at(i1), players.at(i2), playerBounces.at(i1), playerBounces.at(i2));
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
        bounceVec += (bounce.opp->GetMovement() - players.at(i1)->GetMovement()) * bounce.force * (bounce.force / totalForce);
      }

      // okay, accumulated all, now distribute them in normalized fashion
      players.at(i1)->OffsetPosition(bounceVec * 0.01f * 1.0f);
    }
  }
}

void Match::CheckHumanoidCollision(Player *p1, Player *p2,
                                   std::vector<PlayerBounce> &p1Bounce,
                                   std::vector<PlayerBounce> &p2Bounce) {
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

      if (p1 == GetDesignatedPossessionPlayer()) bounceBias += 0.4f;
      if (p2 == GetDesignatedPossessionPlayer()) bounceBias -= 0.4f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
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


      if (GetDesignatedPossessionPlayer() == p2 && p2->HasPossession()) {
        Vector3 p2_leftside = p2pos + p2->GetKinematicState().facing.GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p2_rightside = p2pos + p2->GetKinematicState().facing.GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p1_to_p2_left = (p1pos - p2_leftside).GetLength();
        float p1_to_p2_right = (p1pos - p2_rightside).GetLength();
        Vector3 p2side = p1_to_p2_left < p1_to_p2_right ? p2_leftside : p2_rightside;
        // SetYellowDebugPilon(p2side);
        offset1 += (p2side - p1pos).GetNormalizedMax(0.01f) * p1->GetStat(football::model::PlayerStat::physical_balance) * 0.3f;
      }

      else if (GetDesignatedPossessionPlayer() == p1 && p1->HasPossession()) {
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

      if (p1 == GetDesignatedPossessionPlayer()) similarBias += 0.6f;
      if (p2 == GetDesignatedPossessionPlayer()) similarBias -= 0.6f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
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
      float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
      float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
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
          p1->TripMe((p1->GetKinematicState().velocity * 0.1f + p2->GetKinematicState().velocity * 0.06f + bounceVec * 1.0f).GetNormalized(bounceVec), tripType);
          referee_->TripNotice(p1, p2, tripType);
        }
      }
      if (p2sensitivity > trip0threshold) {
        int tripType = 0;
        if (p2sensitivity > trip1threshold) tripType = 1;
        if (p2sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          p2->TripMe((p2->GetKinematicState().velocity * 0.1f + p1->GetKinematicState().velocity * 0.06f - bounceVec * 1.0f).GetNormalized(-bounceVec), tripType);
          referee_->TripNotice(p2, p1, tripType);
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
        victim->TripMe(tripVec, tripType);
        referee_->TripNotice(victim, tackler, tripType);
      }
    }
  }
}

void Match::CheckBallCollisions() {



  constexpr football::sim::TickSpan kBodyBallCollisionCooldown{15};
  if (now_ <= last_body_ball_collision_tick_ + kBodyBallCollisionCooldown) return;

  std::vector<Player*> players;
  GetTeam(first_team)->GetActivePlayers(players);
  GetTeam(second_team)->GetActivePlayers(players);

  Vector3 bounceVec;
  float bias = 0.0;
  int bounceCount = 0; // this shit is shit, average properly in combination with bias or something like that

  //printf("lasttouchbias: %f, isnul?: %s\n", GetLastTouchBias(200), GetLastTouchBias(200) == 0.0f ? "true" : "false");
  for (int i = 0; i < (signed int)players.size(); i++) {
    const PlayerActionState &action = players[i]->GetSimulationActionState();

    bool biggestRatio = false;
    int teamID = players[i]->GetTeam()->GetID();

    int touchTimeThreshold_ms = 200;//700;
    float oppLastTouchBias = GetTeam(abs(teamID - 1))->GetLastTouchBias(touchTimeThreshold_ms);
    float lastTouchBias = players[i]->GetLastTouchBias(touchTimeThreshold_ms);
    float oppLastTouchBiasLong = GetTeam(abs(teamID - 1))->GetLastTouchBias(1600);

    if (lastTouchBias <= 0.01f &&
        oppLastTouchBias > 0.01f /* && ballTowardsPlayer*/) {
        // cannot collide if opp didn't recently touch ball (we
                      // would be able to predict ball by then), or if player
                      // itself already did (to overcome the 'perpetuum
                      // collision' problem, and to allow for 'controlled ball
                      // collisions' in humanoid class)

      bool collisionAnim = false;
      if (action.type == e_FunctionType_Movement || action.type == e_FunctionType_Trip || action.type == e_FunctionType_Sliding || action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect) collisionAnim = true;
      bool onlyWhenDirectionChangedUnexpectedly = false;
      if (action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect) onlyWhenDirectionChangedUnexpectedly = true;

      bool directionChangedUnexpectedly = false;
      if (onlyWhenDirectionChangedUnexpectedly) {
        float unexpectedDistance = (GetMentalImage(players[i]->GetReactionTime_ms() + static_cast<int>(football::sim::ToMilliseconds(action.elapsed)))->GetBallPrediction(1000) - GetBall()->Predict(1000)).GetLength(); // history from when the action began
        if (unexpectedDistance > 0.5f) directionChangedUnexpectedly = true;
      }


      if (collisionAnim && !players[i]->HasUniquePossession() &&
          (onlyWhenDirectionChangedUnexpectedly ==
           directionChangedUnexpectedly)) {

        float boundingBoxSizeOffset = -0.1f; // fake a big AABB for more blocking fun, or a small one for less bouncy bounce
        if (!players[i]->HasPossession()) boundingBoxSizeOffset += 0.03f; else
                                          boundingBoxSizeOffset -= 0.03f;

        if (action.type == e_FunctionType_Sliding ||
            action.type == e_FunctionType_Interfere) {
          boundingBoxSizeOffset += 0.1f;
        }
        if (action.type == e_FunctionType_Deflect) {
          boundingBoxSizeOffset += 0.2f;
        }

        if (((players[i]->GetPosition() + Vector3(0, 0, 0.8f)) -
             ball->Predict(0))
                .GetLength() < 2.5f) {
            // premature optimization is the root of all evil :D
          const PlayerBodyCollider body =
              BuildBodyCollider(players[i]->GetKinematicState());
          for (const BodyVolume *volume : body.GetVolumes()) {
            float ballRadius = 0.11f + boundingBoxSizeOffset;
            if (volume->IntersectsSphere(ball->Predict(0), ballRadius)) {
              if (players[i] == players[i]
                                    ->GetTeam()
                                    ->GetDesignatedTeamPossessionPlayer() &&
                  GetLastTouchBias(200) < 0.01f) {
                players[i]->TriggerControlledBallCollision();
              } else {
                float movementBias = oppLastTouchBias * 0.8f + 0.2f;
                bounceVec +=
                    (ball->Predict(0) - volume->center)
                        .GetNormalized(Vector3(0)) *
                        movementBias +
                    players[i]->GetMovement() * (1.0f - movementBias);
                bounceCount++;
                players[i]->GetTeam()->SetLastTouchPlayer(
                    players[i], e_TouchType_Accidental);
                // Centrality of the hit, relative to the volume radius. The
                // legacy code normalised by the AABB half diagonal, which
                // inflates the scale for elongated parts; the primitive radius
                // is the more meaningful body extent.
                bias += (1.0f -
                         clamp(((ball->Predict(0) - volume->center).GetLength() -
                                ballRadius) /
                                   volume->radius,
                               0.0f, 1.0f)) *
                            0.9f +
                        0.1f;
              }
            }
          }
        }
      }
    }
  }

  if (bias > 0.0f) {
    bounceVec /= (bounceCount * 1.0f);
    bounceVec.coords[2] *= 0.6f;
    bounceVec.Normalize();
    Vector3 currentMovement = ball->GetMovement();
    Vector3 fullCollisionVec = (bounceVec * 6.0f) + (bounceVec * currentMovement.GetLength() * 0.6f) + (currentMovement * -0.2f);
    bias = clamp(bias, 0.0f, 1.0f);
    bias = bias * 0.5f + 0.5f;
    Vector3 resultVector = fullCollisionVec * bias + currentMovement * (1.0f - bias);
    if (resultVector.GetLength() > currentMovement.GetLength()) resultVector = resultVector.GetNormalized(0) * currentMovement.GetLength();
    //resultVector = resultVector.GetNormalized(0) * (currentMovement.GetLength() * 0.7f + resultVector.GetLength() * 0.3f); // EXPERIMENT!
    resultVector *= 0.7f;

    ball->Touch(resultVector);
    ball->SetRotation(rng_.Uniform(-30, 30), rng_.Uniform(-30, 30),
                      rng_.Uniform(-30, 30), 0.5f * bias);
    last_body_ball_collision_tick_ = now_;
  }
}



void Match::BumpActualTime_ms(unsigned long time) {
  AdvanceTime(football::sim::TickSpanFromMillisecondsExact(time));
}

void Match::AdvanceTime(football::sim::TickSpan delta) {
  using football::sim::TickSpan;
  if (Finished()) return;
  // Check all arithmetic before publishing any clock. Manual advances do not
  // execute rules/physics, so they clip at the current period, not the next half.
  const auto next_tick = now_ + delta;
  TickSpan admitted{};
  if (regulation_running_) {
    const auto half = options_.half_duration;
    const auto limit = matchPhase == MatchPhase::SecondHalf ? half + half : half;
    admitted = TickSpan{std::min(delta.value,
        (limit - std::min(regulation_elapsed_, limit)).value)};
  }
  const auto next_regulation = regulation_elapsed_ + admitted;
  const auto next_in_play = ball_in_play_elapsed_ + (ball_in_play_ ? admitted : TickSpan{});
  now_ = next_tick;
  regulation_elapsed_ = next_regulation;
  ball_in_play_elapsed_ = next_in_play;

  if (ball_in_play_ && !IsInSetPiece()) {
    // Continuous possession window in SI seconds, derived from admitted ticks.
    const float seconds = football::sim::ToSeconds(admitted);
    if (teams[0] == designatedPossessionPlayer->GetTeam()) {
      possession60seconds_ = std::max(possession60seconds_ - seconds, -60.0f);
    } else {
      possession60seconds_ = std::min(possession60seconds_ + seconds, 60.0f);
    }
  }
}
