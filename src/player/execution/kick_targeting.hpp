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


#ifndef FOOTBALL_SIM_PLAYER_KICK_TARGETING_HPP
#define FOOTBALL_SIM_PLAYER_KICK_TARGETING_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/animation/types.hpp"

using football::math::Vector3;

class Player;

namespace football::sim::mechanics {

// Solves the direction and power of an explicitly targeted pass. A live
// `forcedTargetPlayer` takes precedence and is led by its movement; otherwise
// `forcedTargetPosition` is used as a static target point. Returns false when
// neither is available: the caller must reject the pass or keep its last
// explicit solution. This function never selects a receiver.
bool GetPass(Player *player, e_FunctionType passType, int defending_direction,
             Player *forcedTargetPlayer,
             const Vector3 *forcedTargetPosition,
             Vector3 &resultingDirection, float &resultingPower,
             Player *&targetPlayer);

}  // namespace football::sim::mechanics

#endif  // FOOTBALL_SIM_PLAYER_KICK_TARGETING_HPP
