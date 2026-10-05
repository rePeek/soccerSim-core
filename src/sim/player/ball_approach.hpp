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


#ifndef FOOTBALL_SIM_PLAYER_BALL_APPROACH_HPP
#define FOOTBALL_SIM_PLAYER_BALL_APPROACH_HPP

#include "foundation/math/vector3.hpp"

class Match;
class Player;
class MentalImage;

namespace football::sim::mechanics {

// Human input movement assistance toward predicted ball contact.
// This adapts an existing desired movement; it does not choose team tactics.
unsigned int GetToBallMovement(
    Match *match, const MentalImage *mentalImage, Player *player,
    const blunted::Vector3 &desiredDirection, float desiredVelocityFloat,
    blunted::Vector3 &bestDirection, float &bestVelocityFloat,
    blunted::Vector3 &bestLookAt, float haste = 0.0f);
unsigned int GetBallControlMovement(
    const MentalImage *mentalImage, Player *player,
    const blunted::Vector3 &desiredDirection, float desiredVelocityFloat,
    blunted::Vector3 &bestDirection, float &bestVelocityFloat,
    blunted::Vector3 &bestLookAt);

}  // namespace football::sim::mechanics

#endif  // FOOTBALL_SIM_PLAYER_BALL_APPROACH_HPP
