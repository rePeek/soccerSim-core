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


#ifndef FOOTBALL_AI_POSITIONING_HPP
#define FOOTBALL_AI_POSITIONING_HPP

#include <vector>
#include "sim/gamedefines.hpp"

namespace football::ai {

// Tactical attraction/repulsion policy for dribbling and off-ball support.
Vector3 GetForceFieldMovement(const std::vector<ForceSpot> &forceField,
                              const Vector3 &currentPos,
                              float attractorDampingDistance = 10);

}  // namespace football::ai

#endif  // FOOTBALL_AI_POSITIONING_HPP
