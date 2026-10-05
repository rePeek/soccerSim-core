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


#ifndef FOOTBALL_AI_DRIBBLE_POLICY_HPP
#define FOOTBALL_AI_DRIBBLE_POLICY_HPP

#include "foundation/math/vector3.hpp"
#include "support/config/properties.hpp"

class Match;
class Player;
class MentalImage;

namespace football::ai {

void GetBestDribbleMovement(
    Match *match, Player *player, const MentalImage *mentalImage,
    blunted::Vector3 &desiredDirection, float &desiredVelocity,
    const blunted::Properties &teamTactics);

}  // namespace football::ai

#endif  // FOOTBALL_AI_DRIBBLE_POLICY_HPP
