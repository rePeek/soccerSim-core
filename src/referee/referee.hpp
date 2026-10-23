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

#ifndef _HPP_REFEREE
#define _HPP_REFEREE

#include <vector>

#include "football/pitch/pitch.hpp"
#include "football/match/game_mode.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/runtime/phase.hpp"
#include "football/player/foul_assessment.hpp"
#include "referee/ball_touch_facts.hpp"
#include "referee/referee_facts.hpp"
#include "referee/referee_tick_facts.hpp"
#include "referee/referee_view.hpp"
#include "referee/ruling.hpp"

#include "referee/referee_state.hpp"

// Football rules only: no on-pitch official actor or animation is required.
// The referee owns rule state and consumes immutable rule facts; it never
// executes ruling consequences itself, never owns restart execution progress and
// never references a Ball, Player, TeamView or Runtime object. Its only output
// port is the RulingSink.
class Referee {

  public:
    // Rules identify teams by legacy slot (0/1), never by a Team object and
    // never by the restart schedule: the kickoff ceremony is Runtime-owned.
    Referee();
    virtual ~Referee();

    // Mutates only referee facts; lifecycle consequences belong to Simulation.
    void OnPeriodEnded(MatchPhase ended_phase);

    // Rule-state signals from Runtime restart execution: the ceremony resets
    // the stale offside reference and a taken restart abandons its foul.
    void OnCeremonialSetup();
    void OnRestartTaken();

    // Classify this instant's out-of-play condition from the sampled ball and
    // settle it exactly as the boundary fact used to. Simulation calls this at
    // the referee's legacy phase boundary.
    void EvaluateOutOfPlay(const football::sim::rules::RefereeView& view,
                           football::sim::event::RulingSink& rulings);
    // The goal-mouth segment was crossed on `side`; submit the award ruling.
    void GoalMouthCrossed(int side, const football::sim::rules::RefereeTickFacts& facts,
                          football::sim::event::RulingSink* rulings);

    // Pending-foul time advance for the current instant (advantage/expiry).
    void CheckPendingFoul(const football::sim::rules::RefereeView& view,
                          football::sim::event::RulingSink& rulings);

    // Judge one collision from its frozen evidence. `restart_active` is the
    // Runtime-supplied restart gate; a restart in flight suppresses new fouls.
    void AssessFoul(const football::sim::FoulAssessment& fact,
                    football::sim::Tick now, bool restart_active = false);
    // Foul facts carry their own timestamps; the caller supplies the evaluation
    // instant and the live team rule facts (fading possession, static side).
    bool CheckFoul(football::sim::Tick now, const football::pitch::Pitch& pitch,
                   const FoulLiveFacts& live,
                   football::sim::event::RulingSink& rulings);
    // Synchronous accepted-touch consumption (restart release / offside) from
    // facts Runtime sampled at the exact touch boundary.
    void ConsumeBallTouch(const football::sim::rules::BallTouchFacts& facts,
                          football::sim::event::RulingSink& rulings);

  protected:
    // The offside position captured at the last attacking touch (Law 11).
    OffsideSnapshot offside_;

    Foul foul;

  private:
    // Submits a confirmed restart award; Runtime owns the resulting context.
    void SubmitRestart(e_GameMode kind, football::sim::TeamSide team,
                       const football::math::Vector3& home_position,
                       football::sim::TickSpan administration,
                       football::sim::event::RulingSink& rulings,
                       const football::sim::event::RestartCause& cause = {});
    void SettleGoalLine(const football::math::Vector3& ball_pos,
                        const football::sim::rules::RefereeTickFacts& facts,
                        football::sim::event::RulingSink& rulings);
    void SettleTouchline(const football::math::Vector3& ball_pos,
                         const football::sim::rules::RefereeTickFacts& facts,
                         bool post_restart_grace,
                         football::sim::event::RulingSink& rulings);
    void EvaluateBallTouch(const football::sim::rules::BallTouchFacts& facts,
                           football::sim::event::RulingSink& rulings);
    // Shows a card deferred by a realized advantage at the next stoppage. A
    // no-op when no discipline is pending, so every stop path can call it.
    void FlushPendingDiscipline(football::sim::Tick now,
                                football::sim::event::RulingSink& rulings);
};

#endif
