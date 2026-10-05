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

#ifndef _HPP_PLAYERDATA
#define _HPP_PLAYERDATA

#include <string>

#include "model/player.hpp"

// Compatibility facade for legacy simulation callers. The owned model is the
// sole source of identity, appearance and abilities: no separate stats array
// or cached physical_velocity is kept here.
class PlayerData {
 public:
  explicit PlayerData(football::model::Player player);
  PlayerData(int playerDatabaseID, bool left_team);
  PlayerData();
  virtual ~PlayerData();

  const football::model::Player& GetModel() const { return player_; }
  std::string GetLastName() const { return player_.last_name; }
  float GetStat(football::model::PlayerStat stat) const {
    return player_.attributes.get(stat);
  }
  float get_physical_velocity() const {
    return GetStat(football::model::PlayerStat::physical_velocity);
  }
  int GetSkinColor() const { return player_.appearance.skin_color.value(); }
  std::string GetHairStyle() const { return player_.appearance.hair_style; }
  void SetHairStyle(const std::string& style) { player_.appearance.hair_style = style; }
  std::string GetHairColor() const { return player_.appearance.hair_color; }
  float GetHeight() const { return player_.height; }

 private:
  football::model::Player player_;
};

#endif
