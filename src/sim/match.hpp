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
#include "sim/value_history.hpp"

#include "sim/match_options.hpp"
#include "sim/rng.hpp"
#include "model/pitch.hpp"
#include "sim/ai_support/mentalimage.hpp"
#include "sim/animation/library.hpp"
#include "sim/animation/types.hpp"
#include "sim/player_control_set.hpp"
#include "sim/observation_epoch.hpp"
#include "sim/match_phase.hpp"
#include "sim/match_result.hpp"
#include "sim/tick.hpp"
#include "sim/tick_boundary.hpp"


#include <cstdint>
#include <fstream>
#include <iostream>

struct PlayerBounce {
  Player *opp;
  float force = 0.0f;
};

class Match {

  public:
    Match(const football::model::Team& home, const football::model::Team& away,
          const football::model::Pitch& pitch,
          const MatchOptions& options,
          SimulationRng& rng,
          std::shared_ptr<const AnimationLibrary> animation_library);
    virtual ~Match();


    void Exit();
    void Mirror(bool team_0, bool team_1, bool ball);

    int GetScore(int teamID) const { return score_[teamID]; }
    float GetPossessionFactor_60seconds() const { return possession60seconds_ / 60.0f; }
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

    MentalImage* GetMentalImage(int history_ms);
    void UpdateLatestMentalImageBallPredictions();

    void ResetSituation(const Vector3 &focusPos);
    std::uint64_t GetResetSequence() const { return reset_sequence_; }
    const ObservationEpoch& GetObservationEpoch() const { return observation_epoch_; }

    // Half-time change of ends requested by the referee; applied at the start of
    // the next Step, where the canonical between-tick frame is guaranteed.
    void RequestChangeOfEnds() { pending_change_of_ends_ = true; }

    void SetMatchPhase(MatchPhase newMatchPhase);
    MatchPhase GetMatchPhase() const { return matchPhase; }
    bool Finished() const { return matchPhase == MatchPhase::Finished; }
    MatchResult Result() const;

    void StartPlay() { inPlay = true; }
    void StopPlay() { inPlay = false; }
    bool IsInPlay() const { return inPlay; }

    void StartSetPiece() { inSetPiece = true; }
    void StopSetPiece() { inSetPiece = false; }
    bool IsInSetPiece() const { return inSetPiece; }
    Referee *GetReferee() const { return referee_.get(); }

    void SetGoalScored(bool onOff) { if (onOff == false) ballIsInGoal = false; goalScored = onOff; }
    bool IsGoalScored() const { return goalScored; }
    Team* GetLastGoalTeam() const { return lastGoalTeam; }
    void SetLastTouchTeamID(int id, e_TouchType touchType = e_TouchType_Intentional_Kicked) { lastTouchTeamIDs[touchType] = id; lastTouchTeamID = id; referee_->BallTouched(); }
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
    Player *GetBallRetainer() const { return ballRetainer; }
    void SetBallRetainer(Player *retainer) {
      ballRetainer = retainer;
    }

    float GetAveragePossessionSide(int time_ms) const { return possessionSideHistory.GetAverage(time_ms); }

    std::uint64_t GetMatchTime_ms() const { return matchTime_ms; }
    football::sim::Tick GetTimelineTick() const { return now_; }
    void AdvanceTime(football::sim::TickSpan delta);
    // Scheduler-owned fast-forward; retain the existing simulated tail for now.
    void AdvanceToRestartPreparation(football::sim::Tick preparation_tick);
    // Temporary adapters for owners not yet migrated. No millisecond timeline storage.
    unsigned long GetActualTime_ms() const {
      return static_cast<unsigned long>(football::sim::ToMilliseconds(now_));
    }
    void BumpActualTime_ms(unsigned long time);


    // Legacy projection with raw motion, independent of environment cadence.
    // Advances one authoritative simulation tick.
    bool Step(const PlayerControlSet& controls);
    // Legacy direct-match callers have no control source.
    bool Process() { return Step(PlayerControlSet{}); }





    float GetMatchDurationFactor() const { return matchDurationFactor; }

    const std::vector<Vector3> &GetAnimPositionCache(AnimationId animation_id) const;


    int FirstTeam() { return first_team; }
    int SecondTeam() { return second_team; }
    bool isBallMirrored() { return ball_mirrored; }

  private:
    bool CheckForGoal(signed int side, const Vector3& previousBallPos);
    // Mirrors both teams, the ball and mental images onto the other half.
    void SwitchEnds();

    void CalculateBestPossessionTeamID();
    void CheckHumanoidCollisions();
    void CheckHumanoidCollision(Player *p1, Player *p2, std::vector<PlayerBounce> &p1Bounce, std::vector<PlayerBounce> &p2Bounce);
    void CheckBallCollisions();


    int score_[2] = {0, 0};
    float possession60seconds_ = 0.0f;
    const football::model::Pitch pitch_;
    const std::shared_ptr<const AnimationLibrary> animations_;
    SimulationRng& rng_;
    Team *teams[2];
    int first_team = 0;
    int second_team = 1;
    bool ball_mirrored = false;





    Ball *ball = nullptr;

    std::vector<MentalImage> mentalImages; // [index] == index * 10 ms ago ([0] == now)

    std::uint64_t matchTime_ms = 0;
    football::sim::Tick now_{};
    std::uint64_t duration_ticks_ = 0;
    // Actual world discontinuities; not a policy/request timer.
    std::uint64_t reset_sequence_ = 0;
    bool pending_change_of_ends_ = false;
    const ObservationEpoch observation_epoch_ = ObservationEpoch::New();

    MatchPhase matchPhase = MatchPhase::PreMatch;
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


    football::sim::Tick last_body_ball_collision_tick_{};


    std::unique_ptr<Referee> referee_;



    const float matchDurationFactor = 0.0f;

    // Snapshot of initialization-time football rules.
    const MatchOptions options_;
};

#endif
