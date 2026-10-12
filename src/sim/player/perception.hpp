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

#ifndef FOOTBALL_SIM_PLAYER_PERCEPTION_HPP
#define FOOTBALL_SIM_PLAYER_PERCEPTION_HPP

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"

class MentalImage;

namespace football::sim::player {

// Opponent-distribution estimate over a borrowed mental image. Perception data
// for the player's own tactical situation; never a team or AI decision owner.
float CalculateFreeSpace(Tick now, const MentalImage *mentalImage, int teamID,
                         const blunted::Vector3 &focusPos,
                         float safeDistance = 8.0f, float futureTime_sec = 0.3f);

}  // namespace football::sim::player

#endif  // FOOTBALL_SIM_PLAYER_PERCEPTION_HPP
