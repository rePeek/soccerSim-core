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
  referee_ = std::make_unique<Referee>(*teams[first_team], options.ball_position);




}

Match::~Match() {}



void Match::Exit() {
  teams[first_team]->Exit();
  teams[second_team]->Exit();
  delete teams[first_team];
  delete teams[second_team];
  delete ball;
  referee_.reset();


}


void Match::GetActiveTeamPlayers(int teamID, std::vector<Player *> &players) {
  teams[teamID]->GetActivePlayers(players);
}

void Match::SetLastTouchTeamID(int id, e_TouchType touchType,
                              football::sim::rules::RuleCommandSink& commands) {
  lastTouchTeamIDs[touchType] = id;
  lastTouchTeamID = id;
  // Publish explicit facts synchronously; the referee reads no Match state.
  football::sim::rules::BallTouchFacts facts;
  facts.now = GetTimelineTick();
  facts.touch_player = GetLastTouchPlayer();
  facts.touch_team_id = id;
  facts.touch_team = id == -1 ? nullptr : teams[id];
  facts.defending_team = id == -1 ? nullptr : teams[1 - id];
  facts.in_play = IsInPlay();
  facts.in_set_piece = IsInSetPiece();
  facts.offsides_enabled = options_.offsides;
  facts.ball = ball;
  facts.stadium_to_home = PitchFrameTransform(teams[0]->GetStaticSide() != -1);
  std::vector<Player*> active_players;
  if (facts.offsides_enabled) {
    teams[first_team]->GetActivePlayers(active_players);
    teams[second_team]->GetActivePlayers(active_players);
    facts.all_active_players = active_players;
  }
  referee_->BallTouched(facts, commands);
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
