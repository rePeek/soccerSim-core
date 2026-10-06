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
#include "model/team.hpp"
#include "sim/player/player.hpp"

class Match;

class Team {

  public:
    Team(int id, Match *match, const football::model::Team& model,
         float aiDifficulty);
    void Mirror();
    bool isMirrored() {
      return mirrored;
    }
    bool onOriginalSide() {
      return id == 0 ? (side == -1) : (side == 1);
    }

    virtual ~Team();

    void Exit();

    void InitPlayers(std::uint8_t first_schedule_phase);

    Match *GetMatch() { return match; }
    Player *GetPieceTaker();
    e_GameMode GetSetPieceType();

    football::model::TeamSide GetTeamSide() const {
      return static_cast<football::model::TeamSide>(id);
    }
    // Legacy rule-engine slot (home=0, away=1), not persistent team identity.
    int GetID() const { return id; }
    inline signed int GetDynamicSide() {
      return side;
    }
    inline signed int GetStaticSide() {
      return id == 0 ? -1 : 1;
    }
    const football::model::Team& GetModel() const { return model_; }

    FormationEntry GetFormationEntry(void* player);
    void SetFormationEntry(Player* player, FormationEntry entry);
    float GetAiDifficulty() const { return aiDifficulty; }
    const std::vector<Player *> &GetAllPlayers() { return players; }
    void GetAllPlayers(std::vector<Player*> &allPlayers) {
      allPlayers.insert(allPlayers.end(), players.begin(), players.end());
    }
    void GetActivePlayers(std::vector<Player *> &activePlayers);
    int GetActivePlayersCount() const;

    bool HasPossession() const;
    bool HasUniquePossession() const;
    int GetTimeNeededToGetToBall_ms() const;
    Player *GetDesignatedTeamPossessionPlayer() {
      return designatedTeamPossessionPlayer;
    }
    void UpdateDesignatedTeamPossessionPlayer();
    Player *GetBestPossessionPlayer();
    float GetTeamPossessionAmount() const;
    float GetFadingTeamPossessionAmount() const;
    void SetFadingTeamPossessionAmount(float value);

    void SetLastTouchPlayer(
        Player *player, e_TouchType touchType = e_TouchType_Intentional_Kicked);
    Player *GetLastTouchPlayer() const { return lastTouchPlayer; }
    float GetLastTouchBias(int decay_ms, unsigned long time_ms = 0) {
      return lastTouchPlayer
                 ? lastTouchPlayer->GetLastTouchBias(decay_ms, time_ms)
                 : 0;
    }

    void ResetSituation(const Vector3 &focusPos);

    void SetOpponent(Team* opponent) { this->opponent = opponent; }
    Team* Opponent() { return opponent; }
    // Contact/reachability metadata, not input selection or ownership.
    void SetDesignatedTeamPossessionPlayer(Player *player) {
      designatedTeamPossessionPlayer = player;
    }

    void RelaxFatigue(float howMuch);

    void Process();
    void Put2D(bool mirror);
    void Hide2D();

    void UpdatePossessionStats();

    Player *GetGoalie();

  protected:
    const int id;
    Match *match;
    Team *opponent = 0;
    const football::model::Team model_;
    std::vector<FormationEntry> formation_;
    const float aiDifficulty;

    bool hasPossession = false;
    int timeNeededToGetToBall_ms = 0;
    Player *designatedTeamPossessionPlayer = 0;

    float teamPossessionAmount = 0.0f;
    float fadingTeamPossessionAmount = 0.0f;


    std::vector<Player*> players;

    Player *lastTouchPlayer = nullptr;

    int side = -1;
    bool mirrored = false;
};

#endif
