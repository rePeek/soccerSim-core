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


#ifndef FOOTBALL_AI_FORMATION_POLICY_HPP
#define FOOTBALL_AI_FORMATION_POLICY_HPP

#include "sim/gamedefines.hpp"

class Match;
class Player;

namespace football::ai {

Vector3 GetAdaptedFormationPosition(
    Match *match, Player *player, float backXBound, float frontXBound,
    float lowYBound, float highYBound, float xFocus, float xFocusStrength,
    float yFocus, float yFocusStrength, const Vector3 &microFocus,
    float microFocusStrength, float midfieldFocus, float midfieldFocusStrength,
    bool useDynamicFormationPosition = true);
float GetMindSet(e_PlayerRole role);

}  // namespace football::ai

#endif  // FOOTBALL_AI_FORMATION_POLICY_HPP
