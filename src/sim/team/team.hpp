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

#ifndef _HPP_TEAM
#define _HPP_TEAM

#include <cstdint>
#include "football/pitch/pitch.hpp"
#include <memory>
#include <span>
#include "sim/runtime/match_config.hpp"
#include "sim/runtime/team_state.hpp"
#include "sim/runtime/team_view.hpp"
#include "sim/runtime/team_facts.hpp"
#include "sim/runtime/roster.hpp"
#include "sim/player/player.hpp"

class AnimationLibrary;

class MentalImage;

class Team {

  public:
    // Simulation owns the Roster, the TeamFrame and the TeamMatchState; Team
    // borrows all three and owns no authoritative team state of its own.
    Team(int id, football::sim::Roster& roster, football::sim::TeamFrame& frame,
         football::sim::TeamMatchState& state);
    // Mirror/SwitchEnds are runtime behaviour now (Simulation::MirrorTeam and
    // SwitchTeamEnds); Team only exposes the borrowed orientation.
    bool isMirrored() const {
      return frame_.mirrored;
    }
    // True while the team is in its own canonical (attacking) orientation.
    bool onOriginalSide() const {
      return frame_.dynamic_side == frame_.static_side;
    }

    virtual ~Team();



    football::sim::TeamSide GetTeamSide() const {
      return static_cast<football::sim::TeamSide>(id);
    }
    // Legacy rule-engine slot (home=0, away=1), not persistent team identity.
    int GetID() const { return id; }
    // Per-tick processing-frame direction (mirror counter), not persistent.
    inline signed int GetDynamicSide() const {
      return frame_.dynamic_side;
    }
    // Persistent direction this team defends; flips only on a change of ends.
    inline signed int GetStaticSide() const {
      return frame_.static_side;
    }
    std::span<Player *const> GetAllPlayers() const { return roster_.Players(); }
    void GetAllPlayers(std::vector<Player*> &allPlayers) const {
      const auto players = roster_.Players();
      allPlayers.insert(allPlayers.end(), players.begin(), players.end());
    }
    void GetActivePlayers(std::vector<Player *> &activePlayers);
    int GetActivePlayersCount() const;

    bool HasPossession() const;
    int GetTimeNeededToGetToBall_ms() const;
    Player *GetDesignatedTeamPossessionPlayer() {
      return state_.designated_player;
    }
    Player *GetBestPossessionPlayer();
    float GetTeamPossessionAmount() const;
    float GetFadingTeamPossessionAmount() const;
    void SetFadingTeamPossessionAmount(float value);
    // Data publication only; tick/refresh algorithms live in runtime/roster_possession.
    void SetPossessionAmounts(float amount, float fading) {
      state_.possession_amount = amount;
      state_.fading_possession_amount = fading;
      PublishTeamFacts();
    }
    void SetPossessionEstimate(bool has, int arrival_ms) {
      state_.has_possession = has; state_.time_to_ball_ms = arrival_ms;
    }

    // Runtime-owned team match facts, also exposed to players through a
    // borrowed PlayerTeamView. Never a second source of truth.
    football::sim::TeamMatchState &match_state() { return state_; }
    const football::sim::TeamMatchState &match_state() const { return state_; }

    // Transitional adapter while Team still owns the roster: packages the same
    // identity/direction/roster/match-state that Simulation will build directly
    // from Roster + TeamMatchState once Team is deleted (P6).
    football::sim::TeamView view() {
      return football::sim::TeamView{id, GetTeamSide(), frame_.dynamic_side, frame_.static_side,
                                     GetGoalie(), roster_.Players(),
                                     &state_};
    }


    void ResetSituation(const Vector3 &focusPos, football::sim::Tick now);

    void SetOpponent(Team* opponent) { this->opponent = opponent; }
    Team* Opponent() { return opponent; }
    // Contact/reachability metadata, not input selection or ownership.
    void SetDesignatedTeamPossessionPlayer(Player *player) {
      state_.designated_player = player;
      PublishTeamFacts();
    }
    // Publishes the team's current match facts to every player. Player stores a
    // projection only; it never holds a Team object.
    PlayerTeamFacts TeamFacts() const {
      return football::sim::TeamFactsOf(frame_, state_);
    }
    void PublishTeamFacts() {
      football::sim::PublishTeamFacts(roster_, frame_, state_);
    }

    void RelaxFatigue(float howMuch);



    Player *GetGoalie() const;

  protected:
    const int id;
    Team *opponent = 0;
    // Borrowed from Simulation, which owns the only copies.
    football::sim::Roster &roster_;
    football::sim::TeamFrame &frame_;
    football::sim::TeamMatchState &state_;
};

#endif
