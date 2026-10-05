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

#ifndef _HPP_MATCH
#define _HPP_MATCH

#include "sim/team.hpp"
#include "sim/ball.hpp"
#include "sim/referee.hpp"
#include "sim/officials.hpp"
#include "sim/value_history.hpp"

#include "controller/controller_input.hpp"
#include "data/matchdata.hpp"
#include "sim/match_options.hpp"
#include "sim/rng.hpp"
#include "model/pitch.hpp"
#include "sim/ai_support/mentalimage.hpp"
#include "animation/library.hpp"
#include "animation/types.hpp"
#include "control/player_control_set.hpp"



#include <fstream>
#include <iostream>

struct PlayerBounce {
  Player *opp;
  float force = 0.0f;
};

class Match {

  public:
    Match(std::unique_ptr<MatchData> matchData,
          const football::model::Pitch& pitch,
          const MatchOptions& options,
          SimulationRng& rng,
          std::shared_ptr<const AnimationLibrary> animations,
          bool init_animation);
    virtual ~Match();

    void Exit();
    void Mirror(bool team_0, bool team_1, bool ball);

    int GetScore(int teamID) { return matchData->GetGoalCount(teamID); }
    Ball *GetBall() const { return ball; }
    Team *GetTeam(int teamID) const { return teams[teamID]; }
    const football::model::Pitch& pitch() const { return pitch_; }
    // Baked animation library shared by every actor in this match.
    const AnimationLibrary& GetAnimationLibrary() const { return *animations_; }
    // Snapshot of the match rules; never re-read from ambient state.
    const MatchOptions& options() const { return options_; }
    // Deterministic simulation RNG, owned by Simulation and shared by every
    // actor in this match.
    SimulationRng& rng() { return rng_; }
    void GetActiveTeamPlayers(int teamID, std::vector<Player*> &players);
    void GetOfficialPlayers(std::vector<PlayerBase*> &players);

    MentalImage* GetMentalImage(int history_ms);
    void UpdateLatestMentalImageBallPredictions();

    void ResetSituation(const Vector3 &focusPos);

    void SetMatchPhase(e_MatchPhase newMatchPhase);
    e_MatchPhase GetMatchPhase() const { return matchPhase; }

    void StartPlay() { inPlay = true; }
    void StopPlay() { inPlay = false; }
    bool IsInPlay() const { return inPlay; }

    void StartSetPiece() { inSetPiece = true; }
    void StopSetPiece() { inSetPiece = false; }
    bool IsInSetPiece() const { return inSetPiece; }
    Referee *GetReferee() { return referee; }
    Officials *GetOfficials() { return officials; }

    void SetGoalScored(bool onOff) { if (onOff == false) ballIsInGoal = false; goalScored = onOff; }
    bool IsGoalScored() const { return goalScored; }
    Team* GetLastGoalTeam() const { return lastGoalTeam; }
    void SetLastTouchTeamID(int id, e_TouchType touchType = e_TouchType_Intentional_Kicked) { lastTouchTeamIDs[touchType] = id; lastTouchTeamID = id; referee->BallTouched(); }
    int GetLastTouchTeamID(e_TouchType touchType) const { return lastTouchTeamIDs[touchType]; }
    int GetLastTouchTeamID() const { return lastTouchTeamID; }
    Team *GetLastTouchTeam() {
      if (lastTouchTeamID != -1)
        return teams[lastTouchTeamID];
      else
        return teams[first_team];
    }
    Player *GetLastTouchPlayer() {
      if (GetLastTouchTeam())
        return GetLastTouchTeam()->GetLastTouchPlayer();
      else
        return 0;
    }
    float GetLastTouchBias(int decay_ms, unsigned long time_ms = 0) { if (GetLastTouchTeam()) return GetLastTouchTeam()->GetLastTouchBias(decay_ms, time_ms); else return 0; }
    bool IsBallInGoal() const { return ballIsInGoal; }

    Team* GetBestPossessionTeam();

    Player *GetDesignatedPossessionPlayer() { return designatedPossessionPlayer; }
    Player *GetBallRetainer() { return ballRetainer; }
    void SetBallRetainer(Player *retainer) {
      ballRetainer = retainer;
    }

    float GetAveragePossessionSide(int time_ms) const { return possessionSideHistory.GetAverage(time_ms); }

    unsigned long GetMatchTime_ms() const { return matchTime_ms; }
    unsigned long GetActualTime_ms() const { return actualTime_ms; }
    void BumpActualTime_ms(unsigned long time);


    // Legacy projection with raw motion, independent of environment cadence.
    // Advances one authoritative simulation tick.
    bool Step(const PlayerControlSet& controls);
    // Legacy direct-match callers have no control source.
    bool Process() { return Step(PlayerControlSet{}); }




    MatchData* GetMatchData() { return matchData.get(); }

    float GetMatchDurationFactor() const { return matchDurationFactor; }
    bool GetUseMagnet() const { return _useMagnet; }

    const std::vector<Vector3> &GetAnimPositionCache(AnimationId animation_id) const;


    int FirstTeam() { return first_team; }
    int SecondTeam() { return second_team; }
    bool isBallMirrored() { return ball_mirrored; }

  private:
    bool CheckForGoal(signed int side, const Vector3& previousBallPos);

    void CalculateBestPossessionTeamID();
    void CheckHumanoidCollisions();
    void CheckHumanoidCollision(Player *p1, Player *p2, std::vector<PlayerBounce> &p1Bounce, std::vector<PlayerBounce> &p2Bounce);
    void CheckBallCollisions();


    std::unique_ptr<MatchData> matchData;
    const football::model::Pitch pitch_;
    const std::shared_ptr<const AnimationLibrary> animations_;
    SimulationRng& rng_;
    Team *teams[2];
    int first_team = 0;
    int second_team = 1;
    bool ball_mirrored = false;

    Officials *officials;




    Ball *ball = nullptr;

    std::vector<MentalImage> mentalImages; // [index] == index * 10 ms ago ([0] == now)

    unsigned long matchTime_ms = 0;
    unsigned long actualTime_ms = 0;
    unsigned long goalScoredTimer = 0;

    e_MatchPhase matchPhase = e_MatchPhase_PreMatch; // 0 - first half; 1 - second half; 2 - 1st extra time; 3 - 2nd extra time; 4 - penalties
    bool inPlay = false;
    bool inSetPiece = false; // Whether game is in special mode (corner etc...)
    bool goalScored = false; // true after goal scored, false again after next match state change
    bool ballIsInGoal = false;
    Team* lastGoalTeam = 0;
    Player *lastGoalScorer;
    int lastTouchTeamIDs[e_TouchType_SIZE];
    int lastTouchTeamID = 0;
    Team* bestPossessionTeam = 0;
    Player *designatedPossessionPlayer;
    Player *ballRetainer;

    ValueHistory<float> possessionSideHistory;


    unsigned int lastBodyBallCollisionTime_ms = 0;


    Referee *referee;



    const float matchDurationFactor = 0.0f;

    // Snapshot of the initialization options, including the values the referee
    // and team selection used to read from the ambient scenario singleton.
    const MatchOptions options_;
    // Whether to use magnet logic (that automatically pushes active player
    // towards the ball).
    const bool _useMagnet;
};

#endif
