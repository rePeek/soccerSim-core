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

#ifndef _HPP_TEAMDATA
#define _HPP_TEAMDATA

#include <string>
#include <vector>
#include "foundation/math/rng.hpp"
#include "support/config/properties.hpp"

#include "sim/gamedefines.hpp"
#include "data/playerdata.hpp"

// Legacy team database identities. They still select the hardcoded team
// profile (name, colours, kit) used until a team carries it directly.
constexpr int kHomeTeamDatabaseId = 3;
constexpr int kAwayTeamDatabaseId = 8;

// Static inputs for creating one team. An empty `players` selects the
// legacy hardcoded roster; an empty `name` keeps the database name.
struct TeamCreationData {
  int database_id = kHomeTeamDatabaseId;
  std::vector<FormationEntry> formation;
  std::vector<football::model::Player> players;
  std::string name;
};

struct TeamTactics {
  Properties userProperties;
};

class TeamData {

  public:
    explicit TeamData(const TeamCreationData& data, blunted::Rng& rng);
    ~TeamData();

    std::string GetName() const { return name; }
    std::string GetShortName() const { return shortName; }
    std::string GetLogoUrl() const { return logo_url; }
    std::string GetKitUrl() const { return kit_url; }
    Vector3 GetColor1() const { return color1; }
    Vector3 GetColor2() const { return color2; }

    const TeamTactics &GetTactics() const { return tactics; }

    FormationEntry GetFormationEntry(int num) const;
    void SetFormationEntry(int num, FormationEntry entry);

    // vector index# is entry in formation[index#]
    int GetPlayerNum() const { return playerData.size(); }
    PlayerData *GetPlayerData(int num) const { return playerData.at(num); }

   protected:
    std::string name;
    std::string shortName;
    std::string logo_url;
    std::string kit_url;
    Vector3 color1, color2;

    TeamTactics tactics;

    std::vector<FormationEntry> formation;
    std::vector<PlayerData*> playerData;

};

#endif
