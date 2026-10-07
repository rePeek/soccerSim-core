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

#ifndef FOOTBALL_SIM_PLAYER_PLAYER_COMMAND_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_COMMAND_HPP

#include <vector>

#include "foundation/math/vector3.hpp"
#include "sim/animation/types.hpp"

using namespace blunted;

class Player;

// Legacy execution/animation command, distinct from the value-only PlayerControl.
enum e_PlayerCommandModifier {
  e_PlayerCommandModifier_None = 0,
  e_PlayerCommandModifier_KnockOn = 1
};

struct TouchInfo {
  Vector3         inputDirection;
  float           inputPower = 0;

  float           autoDirectionBias = 0;
  float           autoPowerBias = 0;

  Vector3         desiredDirection; // inputdirection after pass function
  float           desiredPower = 0;
  Player          *targetPlayer = 0; // null == do not use
  Player          *forcedTargetPlayer = 0; // null == do not use
};

enum e_StrictMovement {
  e_StrictMovement_False,
  e_StrictMovement_True,
  e_StrictMovement_Dynamic
};

struct PlayerCommand {

  /* specialVar1:

    1: happy celebration
    2: inverse celebration (feeling bad)
    3: referee showing card
  */

  PlayerCommand() {
    desiredFunctionType = e_FunctionType_Movement;
    useDesiredMovement = false;
    desiredVelocityFloat = idleVelocity;
    strictMovement = e_StrictMovement_Dynamic;
    useDesiredLookAt = false;
    useTripType = false;
    useDesiredTripDirection = false;
    onlyDeflectAnimsThatPickupBall = false;
    tripType = 1;
    useSpecialVar1 = false;
    specialVar1 = 0;
    useSpecialVar2 = false;
    specialVar2 = 0;
    modifier = 0;
  }

  e_FunctionType desiredFunctionType;

  bool           useDesiredMovement;
  Vector3        desiredDirection;
  e_StrictMovement strictMovement;

  float          desiredVelocityFloat;

  bool           useDesiredLookAt;
  Vector3        desiredLookAt; // absolute 'look at' position on pitch

  bool           useTouchInfo = false;
  TouchInfo      touchInfo;

  bool           onlyDeflectAnimsThatPickupBall;

  bool           useTripType;
  int            tripType; // only applicable for trip anims

  bool           useDesiredTripDirection;
  Vector3        desiredTripDirection;

  bool           useSpecialVar1;
  int            specialVar1;
  bool           useSpecialVar2;
  int            specialVar2;

  int            modifier;
};

typedef std::vector<PlayerCommand> PlayerCommandQueue;

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_COMMAND_HPP
