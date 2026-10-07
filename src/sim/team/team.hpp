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
#include <span>
#include "model/team.hpp"
#include "sim/player/player.hpp"

class Match;
namespace football::sim { class BallTouchSink; }

class MentalImage;

class Team {

  public:
    Team(int id, Match *match, const football::model::Team& model,
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

    // Team-local touch facts only; publication belongs to the explicit sink.
    void NoteLastTouchPlayer(Player *player, football::sim::Tick now, e_TouchType touchType);
    Player *GetLastTouchPlayer() const { return lastTouchPlayer; }
    float GetLastTouchBias(int decay_ms, std::optional<football::sim::Tick> at = std::nullopt) {
      return lastTouchPlayer
                 ? lastTouchPlayer->GetLastTouchBias(decay_ms, at)
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

    void Process(std::span<MentalImage> history, football::sim::BallTouchSink& touch_sink);
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
    // id==0 ? -1 : 1 initially; flipped once per change of ends.
    int static_side_;
    bool mirrored = false;
};

#endif
