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

#include "sim/team/team.hpp"
#include "sim/ball/ball.hpp"
#include "sim/rules/referee.hpp"

#include "sim/match/match_options.hpp"
#include "sim/match/match_clock.hpp"
#include "sim/match/pitch_geometry.hpp"
#include "sim/random/rng.hpp"
#include "model/pitch.hpp"
#include "sim/animation/library.hpp"
#include "sim/animation/types.hpp"
#include "sim/match/match_phase.hpp"
#include "sim/match/match_result.hpp"
#include "sim/time/tick.hpp"
#include "sim/time/tick_boundary.hpp"
#include "sim/event/touch_query.hpp"
namespace football::sim::testing { class SimulationAccess; }


#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>

class Match {

  public:
    Match(const football::model::Team& home, const football::model::Team& away,
          const football::model::Pitch& pitch,
          const MatchOptions& options,
          SimulationRng& rng,
          std::shared_ptr<const AnimationLibrary> animation_library,
          football::sim::MatchClock& clock);
    virtual ~Match();


    void Exit();

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

    std::uint64_t GetResetSequence() const { return reset_sequence_; }

    // Half-time change of ends scheduled by Simulation; applied at the start of
    // the next Step, where the canonical between-tick frame is guaranteed.
    void RequestChangeOfEnds() { pending_change_of_ends_ = true; }

    void SetMatchPhase(MatchPhase newMatchPhase);
    MatchPhase GetMatchPhase() const { return matchPhase; }
    bool Finished() const { return matchPhase == MatchPhase::Finished; }
    MatchResult Result() const;

    // Authorization lets a Ready taker execute; it does not make the ball live.
    void StartPlay() { inPlay = true; }
    void StopPlay() { inPlay = false; clock_.StopBallInPlay(); }
    const bool& IsInPlay() const { return inPlay; }
    // Referee marks the actual accepted restart contact, including both half kickoffs.
    void StartBallInPlay();
    void EndHalf() { StopPlay(); StopSetPiece(); clock_.EndHalf(); }
    bool IsHalfUnderway() const { return clock_.IsHalfUnderway(); }
    const bool& IsBallInPlay() const { return clock_.IsBallInPlay(); }
    bool MayTouchBall(const Player& actor) const {
      const auto& restart = referee_->GetBuffer();
      return IsBallInPlay() || (inPlay && inSetPiece && restart.active &&
                              restart.taker == &actor);
    }

    void StartSetPiece() { inSetPiece = true; }
    void StopSetPiece() { inSetPiece = false; }
    const bool& IsInSetPiece() const { return inSetPiece; }
    Referee *GetReferee() const { return referee_; }

    void SetGoalScored(bool onOff) {
      if (onOff) clock_.StopBallInPlay();
      else ballIsInGoal = false;
      goalScored = onOff;
    }
    bool IsGoalScored() const { return goalScored; }
    Team* GetLastGoalTeam() const { return lastGoalTeam; }
    int GetLastTouchTeamID(e_TouchType touchType) const { return touches_.last_team_by_type[touchType]; }
    int GetLastTouchTeamID() const { return touches_.last_team; }
    const football::sim::event::TouchState& touches() const { return touches_; }
    Team *GetLastTouchTeam() {
      if (touches_.last_team != -1)
        return teams[touches_.last_team];
      else
        return teams[first_team];
    }
    Player *GetLastTouchPlayer() {
      return football::sim::event::LastTouchPlayer(touches_, *GetLastTouchTeam());
    }
    bool IsBallInGoal() const { return ballIsInGoal; }
    football::sim::BallEnvironment GetBallEnvironment() const { return {ballIsInGoal}; }

    Team* GetBestPossessionTeam();

    Player *GetDesignatedPossessionPlayer() { return designatedPossessionPlayer; }
    Player *GetBallRetainer() const { return ballRetainer; }
    void SetBallRetainer(Player *retainer) {
      ballRetainer = retainer;
    }


    football::sim::TickSpan GetRegulationTime() const { return clock_.RegulationTime(); }
    football::sim::TickSpan GetBallInPlayTime() const { return clock_.BallInPlayTime(); }
    football::sim::Tick GetTimelineTick() const { return clock_.now(); }




    int FirstTeam() { return first_team; }
    int SecondTeam() { return second_team; }
    bool isBallMirrored() { return ball_mirrored; }

  private:
    friend class Simulation;
    friend class football::sim::testing::SimulationAccess;


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


    // Transitional clock projection/commands; lifetime owned by Simulation.
    football::sim::MatchClock& clock_;
    // Actual world discontinuities; not a policy/request timer.
    std::uint64_t reset_sequence_ = 0;
    bool pending_change_of_ends_ = false;

    MatchPhase matchPhase = MatchPhase::PreMatch;
    bool inPlay = false;
    bool inSetPiece = false; // Whether game is in special mode (corner etc...)
    bool goalScored = false; // true after goal scored, false again after next match state change
    bool ballIsInGoal = false;
    Team* lastGoalTeam = 0;
    Player *lastGoalScorer;
    football::sim::event::TouchState touches_;
    Team* bestPossessionTeam = 0;
    Player *designatedPossessionPlayer;
    Player *ballRetainer;



    football::sim::Tick last_body_ball_collision_tick_{};


    // Transitional non-owning rule access for actors; owned by Simulation.
    Referee* referee_ = nullptr;




    // Snapshot of initialization-time football rules.
    const MatchOptions options_;
};

#endif
