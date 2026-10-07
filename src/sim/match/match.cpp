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

#include <algorithm>
#include <stdexcept>






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
      clock_(options.half_duration),
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
  teams[first_team]->InitPlayers(0, *animations_);
  teams[second_team]->InitPlayers(static_cast<std::uint8_t>(
      teams[first_team]->GetAllPlayers().size() % 10), *animations_);

  std::vector<Player*> activePlayers;
  teams[first_team]->GetActivePlayers(activePlayers);
  designatedPossessionPlayer = activePlayers.at(0);
  ballRetainer = 0;


  // match params

  lastGoalTeam = 0;
  lastGoalScorer = 0;
  bestPossessionTeam = 0;
  SetMatchPhase(MatchPhase::PreMatch);





}

Match::~Match() {}



void Match::Exit() {
  teams[first_team]->Exit();
  teams[second_team]->Exit();
  delete teams[first_team];
  delete teams[second_team];
  delete ball;


}


void Match::GetActiveTeamPlayers(int teamID, std::vector<Player *> &players) {
  teams[teamID]->GetActivePlayers(players);
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
  clock_.BeginHalf();
  clock_.StartBallInPlay();
}

MatchResult Match::Result() const {
  if (!Finished()) throw std::logic_error("match result requires full time");
  const auto outcome = score_[0] > score_[1] ? MatchOutcome::HomeWin
      : score_[0] < score_[1] ? MatchOutcome::AwayWin : MatchOutcome::Draw;
  return {score_[0], score_[1], outcome, clock_.ExecutedTicks()};
}

Team *Match::GetBestPossessionTeam() {
  return bestPossessionTeam;
}
