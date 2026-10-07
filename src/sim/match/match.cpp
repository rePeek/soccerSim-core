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

#include "sim/match/match.hpp"
#include "sim/animation/library.hpp"

#include "sim/rules/goal.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <cassert>
#include "sim/player/player_contact.hpp"
#include "sim/observation/mentalimage_sampling.hpp"

using football::sim::observation::kMentalImageCadence;




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
      options_(options) {




  ball = new Ball(pitch_);

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

MentalImage *Match::GetMentalImage(football::sim::TickSpan history) {
  return &mentalImages[football::sim::observation::MentalImageSampleIndex(mentalImages.size(), history)];
}

MentalImage *Match::GetMentalImage(std::chrono::milliseconds history) {
  return &mentalImages[football::sim::observation::MentalImageSampleIndex(mentalImages.size(), history)];
}

void Match::UpdateLatestMentalImageBallPredictions() {
  if (!mentalImages.empty()) mentalImages[0].UpdateBallPredictions();
}

void Match::TouchBall(const Vector3& impulse) {
  // Keep the legacy ordering, including pre-rotation observer/possession refresh.
  ball->Touch(impulse, GetBallEnvironment());
  UpdateLatestMentalImageBallPredictions();
  teams[first_team]->UpdatePossessionStats();
  teams[second_team]->UpdatePossessionStats();
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




// Transitional remainder, not a second public tick entry point.
bool Match::StepRemainingTick() {
  bool reverse = options_.reverse_team_processing;
  referee_->Process();
  Vector3 previousBallPos = ball->Predict(0);
  Mirror(reverse, !reverse, false);
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
  ball->Process(GetBallEnvironment());
  Mirror(false, false, reverse);

  // create mental images for the AI to use
  if (mentalImages.empty() || now_.value % kMentalImageCadence.value == 0) {
    mentalImages.insert(mentalImages.begin(), MentalImage(this));
    if (mentalImages.size() > 3) {
      mentalImages.pop_back();
    }
  }

  Mirror(first_team == 1, first_team == 0, false);
  teams[first_team]->Process();
  Mirror(true, true, true);
  teams[second_team]->Process();
  Mirror(first_team == 0, first_team == 1, true);

  Mirror(first_team == 1, first_team == 0, false);
  teams[first_team]->UpdatePossessionStats();
  Mirror(true, true, true);
  teams[second_team]->UpdatePossessionStats();
  Mirror(first_team == 0, first_team == 1, true);

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

  Mirror(reverse, !reverse, false);
  std::vector<Player*> players;
  GetTeam(first_team)->GetActivePlayers(players);
  GetTeam(second_team)->GetActivePlayers(players);
  football::sim::ResolvePlayerContacts(
      {players, *ball, designatedPossessionPlayer}, *referee_);

  AdvanceTime(football::sim::TickSpan{1});

  // check for goals
  bool first_team_goal = false;
  bool second_team_goal = false;
  if (IsBallInPlay()) {
    // Retain the per-side legacy lookahead gate; geometry itself needs no Ball.
    const auto crossed_goal = [&](int side) {
      if (fabs(ball->Predict(10).coords[0]) < pitch_.half_length() - 1.0) return false;
      return football::sim::CrossedGoalLine(
          pitch_, side, previousBallPos, ball->Predict(0));
    };
    first_team_goal = crossed_goal(teams[first_team]->GetDynamicSide());
    second_team_goal = crossed_goal(teams[second_team]->GetDynamicSide());
  }
  bool goal = first_team_goal | second_team_goal;
  ballIsInGoal |= goal;
  Mirror(reverse, !reverse, false);
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
  return true;
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
