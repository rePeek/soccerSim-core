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

#ifndef FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_HPP
#define FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_HPP


#include "foundation/math/vector3.hpp"

#include "sim/observation/player_image.hpp"
#include "sim/time/tick.hpp"

using namespace blunted;

class Match;
class Player;

class MentalImage {
 public:
  MentalImage() { }
  MentalImage(Match *match);
  void Mirror(bool team_0, bool team_1, bool ball);
  PlayerImage GetPlayerImage(Player* player) const;
  std::vector<PlayerImagePosition> GetTeamPlayerImages(int teamID) const;
  void UpdateBallPredictions();
  Vector3 GetBallPrediction(football::sim::TickSpan horizon) const;
  // Transitional horizon adapter for not-yet-migrated calculation callers.
  Vector3 GetBallPrediction(int time_ms) const;
  football::sim::TickSpan GetAge() const;

  std::vector<PlayerImage> players;
  std::vector<Vector3> ballPredictions;
  football::sim::Tick captured_tick{};
  float maxDistanceDeviation = 2.5f;
  float maxMovementDeviation = walkVelocity;
  bool ballPredictions_mirrored = false;
 private:
  Match *match = nullptr;
};

#endif
