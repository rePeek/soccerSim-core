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
#include <optional>

#include "football/player/player_role.hpp"
#include "football/referee/game_mode.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/runtime/phase.hpp"
#include "runtime/restart_context.hpp"
#include "referee/ball_touch_facts.hpp"
#include "football/pitch/pitch_frame.hpp"
#include "referee/referee_tick_facts.hpp"
#include "referee/referee_view.hpp"
#include "football/runtime/simulation_config.hpp"
#include "event/accepted_touch.hpp"
#include "player/foul_assessment.hpp"
#include "referee/ruling.hpp"
#include "football/foundation/math/rng.hpp"


using namespace football::math;

class Player;

#include "referee/referee_state.hpp"

// Football rules only: no on-pitch official actor or animation is required.
// The referee owns rule state and consumes immutable facts; it never executes
// ruling consequences itself and never owns restart execution progress. Its only
// output port is the RulingSink: it never issues runtime commands.
class Referee {

  public:
    // Rules identify teams by legacy slot (0/1), never by a Team object and
    // never by the restart schedule: the kickoff ceremony is Runtime-owned.
    Referee();
    virtual ~Referee();

    // Mutates only referee facts; lifecycle consequences belong to Simulation.
    void OnPeriodEnded(MatchPhase ended_phase, football::sim::Tick now,
                       const Vector3& kickoff_position, int kickoff_team_id);

    // Rule-state signals from Runtime restart execution: the ceremony resets
    // the stale offside reference and a taken restart abandons its foul.
    void OnCeremonialSetup();
    void OnRestartTaken();

    // Classify this instant's out-of-play condition from the live ball and
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
    // Runtime-owned restart gate; a restart in flight suppresses new fouls.
    void AssessFoul(const football::sim::FoulAssessment& fact,
                    football::sim::Tick now, bool restart_active = false);
    // Foul facts carry their own timestamps; the caller supplies the evaluation
    // instant and the live team rule facts (fading possession, static side).
    bool CheckFoul(football::sim::Tick now, const football::pitch::Pitch& pitch,
                   PitchFrameTransform stadium_to_home, const FoulLiveFacts& live,
                   football::sim::event::RulingSink& rulings);
    // Synchronous accepted-touch consumption (restart release / offside).
    void ConsumeBallTouch(football::sim::Tick now,
                          const football::sim::event::AcceptedTouch& fact,
                          const football::sim::rules::RefereeView& view,
                          football::sim::event::RulingSink& rulings);

  protected:
    // Players on offside position at the time of the last ball touch.
    std::vector<OffsideReference> offsidePlayers;

    Foul foul;

  private:
    // Submits a confirmed restart award; Runtime owns the resulting context.
    void SubmitRestart(e_GameMode kind, football::sim::TeamSide team,
                       const Vector3& home_position, football::sim::TickSpan administration,
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
    Player* FindPlayer(const football::sim::rules::RefereeTickFacts& facts,
                       football::player::PlayerId id) const;
};

#endif
