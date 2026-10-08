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
#include "sim/time/tick.hpp"
#include "sim/rules/phase.hpp"
#include "sim/rules/restart_readiness.hpp"
#include "sim/rules/ball_touch_facts.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/rules/referee_tick_facts.hpp"
#include "sim/rules/rule_command_sink.hpp"
#include "sim/simulation_config.hpp"
#include "sim/random/rng.hpp"


using namespace blunted;

class Player;
class Team;

struct RestartPolicy {
  football::sim::TickSpan minimum_delay;
  football::sim::TickSpan maximum_delay;
};
// Explicit restart-scheduling facts; the referee state machine reads no Match here.
struct RestartSchedule {
  football::sim::Tick now{};
  Team* setpiece_team = nullptr;
  PitchFrameTransform frame{false};
};
enum class RestartPhase { Pending, Ready, Taken, InPlay };
struct RestartState {
  football::sim::Tick entered_tick{};
  football::sim::Tick earliest_restart_tick{};
  football::sim::Tick timeout_tick{};
  RestartPhase phase = RestartPhase::Pending;
  bool setup_done = false;
  bool used_timeout_placement = false;
  RestartPlan plan;
};

struct RefereeBuffer {
  // Referee has pending action to execute.
  bool active = false;
  e_GameMode desiredSetPiece;
  signed int teamID = 0;
  Team* setpiece_team = 0;
  football::sim::Tick stop_tick{};
  football::sim::Tick prepare_tick{};
  football::sim::Tick start_tick{};
  Vector3 restartPos;
  Player *taker = nullptr;
  bool endPhase = false;
  std::optional<RestartState> restart;
};

struct Foul {
  Player *foulPlayer = 0;
  Player *foulVictim = 0;
  int foulType = 0; // 0: nothing, 1: foul, 2: yellow, 3: red
  bool advantage = false;
  football::sim::Tick foul_tick{};
  Vector3 foulPosition;
  bool hasBeenProcessed = false;
};

// Football rules only: no on-pitch official actor or animation is required.
class Referee {

  public:
    Referee(Team& kickoff_team, const Vector3& kickoff_position);
    virtual ~Referee();

    // Mutates only referee facts; lifecycle consequences belong to Simulation.
    void OnPeriodEnded(MatchPhase ended_phase, football::sim::Tick now,
                       const Vector3& kickoff_position, Team& kickoff_team);

    // Tick-local facts and synchronous write-only consequences; never stored.
    void Process(const football::sim::rules::RefereeTickFacts& facts,
                 const MatchOptions& options, SimulationRng& rng,
                 football::sim::rules::RuleCommandSink& commands);

    const RefereeBuffer &GetBuffer() { return buffer; };
    bool RestartNeedsSimulation() const;
    std::optional<Vector3> GetRestartTarget(const Player* player) const;

    // Consumes explicit competition/observation facts; never reads Match.
    void BallTouched(const football::sim::rules::BallTouchFacts& facts,
                     football::sim::rules::RuleCommandSink& commands);
    // Synchronous foul-state operation: explicit clock/Ball facts, live actor reads.
    // Types: 1 = little standing trip, 2 = standing fall, 3 = sliding tackle.
    void TripNotice(Player *tripee, Player *tripper, int tackleType,
                    football::sim::Tick now, const Vector3& ball_position);
    // Foul facts carry their own timestamps; the caller supplies the evaluation instant.
    bool CheckFoul(football::sim::Tick now, PitchFrameTransform stadium_to_home,
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
                        const MatchOptions& options, SimulationRng& rng,
                        football::sim::rules::RuleCommandSink& commands);
    void PrepareCeremonialKickOff(const football::sim::rules::RefereeTickFacts& facts,
                                 const MatchOptions& options, SimulationRng& rng,
                                 football::sim::rules::RuleCommandSink& commands);
};

#endif
