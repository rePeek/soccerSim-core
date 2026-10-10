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
#include <optional>

#include "model/football_types.hpp"
#include "foundation/time/tick.hpp"
#include "sim/runtime/phase.hpp"
#include "sim/referee/restart_readiness.hpp"
#include "sim/referee/ball_touch_facts.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/referee/referee_tick_facts.hpp"
#include "sim/referee/referee_view.hpp"
#include "sim/referee/rule_command_sink.hpp"
#include "sim/simulation_config.hpp"
#include "sim/event/accepted_touch.hpp"
#include "sim/player/foul_assessment.hpp"
#include "sim/referee/ruling.hpp"
#include "foundation/math/rng.hpp"


using namespace blunted;

class Player;
class Team;

#include "sim/referee/referee_state.hpp"

// Football rules only: no on-pitch official actor or animation is required.
// The referee owns rule state and consumes immutable facts; it never executes
// ruling consequences itself (Simulation owns the write-only rule port).
class Referee {

  public:
    Referee(Team& kickoff_team, const Vector3& kickoff_position);
    virtual ~Referee();

    // Mutates only referee facts; lifecycle consequences belong to Simulation.
    void OnPeriodEnded(MatchPhase ended_phase, football::sim::Tick now,
                       const Vector3& kickoff_position, Team& kickoff_team);


    // Time-only state advance for one instant: pending restart administration,
    // opening/half-time ceremonies and the post-restart grace countdown. No
    // ball/line facts are produced or consumed here.
    void Advance(const football::sim::rules::RefereeView& view,
                 const MatchOptions& options, blunted::Rng& rng,
                 football::sim::rules::RuleCommandSink& commands);

    // Classify this instant's out-of-play condition from the live ball and
    // settle it exactly as the boundary fact used to. Simulation calls this at
    // the referee's legacy phase boundary.
    void EvaluateOutOfPlay(const football::sim::rules::RefereeView& view,
                           football::sim::rules::RuleCommandSink& commands);
    // The goal-mouth segment was crossed on `side`; submit the award ruling.
    void GoalMouthCrossed(int side, const football::sim::rules::RefereeTickFacts& facts,
                          football::sim::event::RulingSink* rulings);

    // Pending-foul time advance for the current instant (advantage/expiry).

    void CheckPendingFoul(const football::sim::rules::RefereeView& view,
                          football::sim::rules::RuleCommandSink& commands);


    const RefereeBuffer &GetBuffer() { return buffer; };
    bool RestartNeedsSimulation() const;
    std::optional<Vector3> GetRestartTarget(const Player* player) const;

    // Consumes explicit competition/observation facts; never reads Match.
    void BallTouched(const football::sim::rules::BallTouchFacts& facts,
                     football::sim::rules::RuleCommandSink& commands);
    // Test-only adapter for the frozen standing/sliding challenge scenarios.
    // Types: 1 = little standing trip (never a foul), 2 = standing fall,
    // 3 = sliding tackle. Production feeds AssessFoul from the solver.
    void TripNotice(Player *tripee, Player *tripper, int tackleType,
                    football::sim::Tick now, const Vector3& ball_position);
    // Judge one collision from its frozen evidence. Production resolves the
    // assessment identities to live actors before calling this; the referee
    // never reads collision math or later actor state here.
    void AssessFoul(Player* victim, Player* offender,
                    const football::sim::FoulAssessment& fact,
                    football::sim::Tick now);
    // Foul facts carry their own timestamps; the caller supplies the evaluation instant.
    bool CheckFoul(football::sim::Tick now, const football::model::Pitch& pitch,
                   PitchFrameTransform stadium_to_home,
                   football::sim::rules::RuleCommandSink& commands);
    // Synchronous accepted-touch consumption (restart release / offside).
    void ConsumeBallTouch(football::sim::Tick now,
                          const football::sim::event::AcceptedTouch& fact,
                          const football::sim::rules::RefereeView& view,
                          football::sim::rules::RuleCommandSink& commands);

    Player *GetCurrentFoulPlayer() { return foul.foulPlayer; }
    int GetCurrentFoulType() { return foul.foulType; }

  protected:
    RefereeBuffer buffer;

    // Ignore the ball outside the line just after a throw-in is taken.
    football::sim::TickSpan post_restart_relax_{};

    // Players on offside position at the time of the last ball touch.
    std::vector<Player*> offsidePlayers;

    Foul foul;

  private:
    void ScheduleRestart(const RestartSchedule& schedule,
                         football::sim::TickSpan administration = {});
    void ProcessRestart(const football::sim::rules::RefereeTickFacts& facts,
                        const MatchOptions& options, blunted::Rng& rng,
                        football::sim::rules::RuleCommandSink& commands);
    void PrepareCeremonialKickOff(const football::sim::rules::RefereeTickFacts& facts,
                                 const MatchOptions& options, blunted::Rng& rng,
                                 football::sim::rules::RuleCommandSink& commands);
    void EvaluateBallTouch(const football::sim::rules::BallTouchFacts& facts,
                           football::sim::rules::RuleCommandSink& commands);

    void SettleGoalLine(const blunted::Vector3& ball_pos,
                        const football::sim::rules::RefereeTickFacts& facts,
                        football::sim::rules::RuleCommandSink& commands);
    void SettleTouchline(const blunted::Vector3& ball_pos,
                         const football::sim::rules::RefereeTickFacts& facts,
                         football::sim::rules::RuleCommandSink& commands);
    Player* FindPlayer(const football::sim::rules::RefereeTickFacts& facts,
                       football::model::PlayerId id) const;
};

#endif
