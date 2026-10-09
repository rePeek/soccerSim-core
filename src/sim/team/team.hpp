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
#include <memory>
#include <span>
#include "model/team.hpp"
#include "sim/player/player.hpp"

class AnimationLibrary;

class MentalImage;

class Team {

  public:
    Team(int id, const football::model::Team& model,
         float aiDifficulty);
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

    void Exit(football::sim::Tick now);

    void InitPlayers(std::uint8_t first_schedule_phase, const AnimationLibrary& animations, const football::model::Pitch& pitch, blunted::Rng& rng);



    football::model::TeamSide GetTeamSide() const {
      return static_cast<football::model::TeamSide>(id);
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
    const football::model::Team& GetModel() const { return model_; }

    FormationEntry GetFormationEntry(void* player);
    void SetFormationEntry(Player* player, FormationEntry entry);
    float GetAiDifficulty() const { return aiDifficulty; }
    const std::vector<Player *> &GetAllPlayers() const { return player_views_; }
    void GetAllPlayers(std::vector<Player*> &allPlayers) {
      allPlayers.insert(allPlayers.end(), player_views_.begin(), player_views_.end());
    }
    void GetActivePlayers(std::vector<Player *> &activePlayers);
    int GetActivePlayersCount() const;

    bool HasPossession() const;
    int GetTimeNeededToGetToBall_ms() const;
    Player *GetDesignatedTeamPossessionPlayer() {
      return designatedTeamPossessionPlayer;
    }
    Player *GetBestPossessionPlayer();
    float GetTeamPossessionAmount() const;
    float GetFadingTeamPossessionAmount() const;
    void SetFadingTeamPossessionAmount(float value);
    // Data publication only; tick/refresh algorithms live in player/possession.
    void SetPossessionAmounts(float amount, float fading) {
      teamPossessionAmount = amount; fadingTeamPossessionAmount = fading;
    }
    void SetPossessionEstimate(bool has, int arrival_ms) {
      hasPossession = has; timeNeededToGetToBall_ms = arrival_ms;
    }


    void ResetSituation(const Vector3 &focusPos, football::sim::Tick now);

    void SetOpponent(Team* opponent) { this->opponent = opponent; }
    Team* Opponent() { return opponent; }
    // Contact/reachability metadata, not input selection or ownership.
    void SetDesignatedTeamPossessionPlayer(Player *player) {
      designatedTeamPossessionPlayer = player;
    }

    void RelaxFatigue(float howMuch);



    Player *GetGoalie();

  protected:
    const int id;
    Team *opponent = 0;
    const football::model::Team model_;
    std::vector<FormationEntry> formation_;
    const float aiDifficulty;

    bool hasPossession = false;
    int timeNeededToGetToBall_ms = 0;
    Player *designatedTeamPossessionPlayer = 0;

    float teamPossessionAmount = 0.0f;
    float fadingTeamPossessionAmount = 0.0f;


    // Ownership; raw views below are stable for the whole team lifetime.
    std::vector<std::unique_ptr<Player>> players_;
    std::vector<Player*> player_views_;


    int side = -1;
    // id==0 ? -1 : 1 initially; flipped once per change of ends.
    int static_side_;
    bool mirrored = false;
};

#endif
