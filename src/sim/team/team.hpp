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
#include "sim/runtime/roster.hpp"
#include "sim/player/player.hpp"

class AnimationLibrary;

class MentalImage;

class Team {

  public:
    // Simulation owns the Roster and the players' lifetime; Team only borrows it.
    // `roster` must outlive this Team.
    Team(int id, const football::sim::TeamSetup& model,
         football::sim::Roster& roster, float aiDifficulty);
    void Mirror();
    // Half-time change of ends: this team attacks the opposite goal from now on.
    // Mirrors its actors like Mirror() but leaves the canonical between-tick frame
    // (mirrored == false), so CheckCanonicalFrame still holds afterwards.
    void SwitchEnds();
    bool isMirrored() {
      return mirrored;
    }
    // True while the team is in its own canonical (attacking) orientation.
    bool onOriginalSide() {
      return side == static_side_;
    }

    virtual ~Team();



    football::sim::TeamSide GetTeamSide() const {
      return static_cast<football::sim::TeamSide>(id);
    }
    // Legacy rule-engine slot (home=0, away=1), not persistent team identity.
    int GetID() const { return id; }
    // Per-tick processing-frame direction (mirror counter), not persistent.
    inline signed int GetDynamicSide() const {
      return side;
    }
    // Persistent direction this team defends; flips only on a change of ends.
    inline signed int GetStaticSide() const {
      return static_side_;
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
      return match_state_.designated_player;
    }
    Player *GetBestPossessionPlayer();
    float GetTeamPossessionAmount() const;
    float GetFadingTeamPossessionAmount() const;
    void SetFadingTeamPossessionAmount(float value);
    // Data publication only; tick/refresh algorithms live in runtime/roster_possession.
    void SetPossessionAmounts(float amount, float fading) {
      match_state_.possession_amount = amount;
      match_state_.fading_possession_amount = fading;
      PublishTeamFacts();
    }
    void SetPossessionEstimate(bool has, int arrival_ms) {
      match_state_.has_possession = has; match_state_.time_to_ball_ms = arrival_ms;
    }

    // Runtime-owned team match facts, also exposed to players through a
    // borrowed PlayerTeamView. Never a second source of truth.
    football::sim::TeamMatchState &match_state() { return match_state_; }
    const football::sim::TeamMatchState &match_state() const { return match_state_; }

    // Transitional adapter while Team still owns the roster: packages the same
    // identity/direction/roster/match-state that Simulation will build directly
    // from Roster + TeamMatchState once Team is deleted (P6).
    football::sim::TeamView view() {
      return football::sim::TeamView{id, GetTeamSide(), side, static_side_,
                                     GetGoalie(), roster_.Players(),
                                     &match_state_};
    }


    void ResetSituation(const Vector3 &focusPos, football::sim::Tick now);

    void SetOpponent(Team* opponent) { this->opponent = opponent; }
    Team* Opponent() { return opponent; }
    // Contact/reachability metadata, not input selection or ownership.
    void SetDesignatedTeamPossessionPlayer(Player *player) {
      match_state_.designated_player = player;
      PublishTeamFacts();
    }
    // Publishes the team's current match facts to every player. Player stores a
    // projection only; it never holds a Team object.
    PlayerTeamFacts TeamFacts() const {
      return PlayerTeamFacts{side, static_side_, side == static_side_,
                             match_state_.possession_amount,
                             match_state_.fading_possession_amount,
                             match_state_.designated_player};
    }
    void PublishTeamFacts() {
      for (Player* p : roster_.Players()) p->SetTeamContext(TeamFacts());
    }

    void RelaxFatigue(float howMuch);



    Player *GetGoalie() const;

  protected:
    const int id;
    Team *opponent = 0;
    const football::sim::TeamSetup model_;
    const float aiDifficulty;

    // Team match facts shared with players through a borrowed view.
    football::sim::TeamMatchState match_state_;


    // Borrowed from Simulation (which owns it); Team never clears or frees it.
    football::sim::Roster &roster_;


    int side = -1;
    // id==0 ? -1 : 1 initially; flipped once per change of ends.
    int static_side_;
    bool mirrored = false;
};

#endif
