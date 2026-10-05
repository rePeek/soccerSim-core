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

#ifndef _HPP_ANIMATION_SELECTION_QUERY
#define _HPP_ANIMATION_SELECTION_QUERY

#include <string>

#include "foundation/math/vector3.hpp"
#include "sim/animation/types.hpp"

// The subset of clip metadata used to refine a selection. This is deliberately
// separate from the legacy Animation variable cache: baked runtime selection
// needs typed values, not source-asset parsing state.
struct SelectionProperties {
  std::string incoming_special_state;
  std::string incoming_retain_state;
  float special_var1 = 0.0f;
  float special_var2 = 0.0f;
};

struct CrudeSelectionQuery {
  bool byFunctionType = false;
  e_FunctionType functionType;

  bool byFoot = false;
  e_Foot foot = e_Foot_Left;

  bool heedForcedFoot = false;
  e_Foot strongFoot = e_Foot_Right;

  bool bySide = false;
  blunted::Vector3 lookAtVecRel;

  bool allowLastDitchAnims = false;

  bool byIncomingVelocity = false;
  bool incomingVelocity_Strict = false;
  bool incomingVelocity_NoDribbleToIdle = false;
  bool incomingVelocity_NoDribbleToSprint = false;
  bool incomingVelocity_ForceLinearity = false;
  e_Velocity incomingVelocity;

  bool byOutgoingVelocity = false;
  e_Velocity outgoingVelocity;

  bool byPickupBall = false;
  bool pickupBall = true;

  bool byIncomingBodyDirection = false;
  blunted::Vector3 incomingBodyDirection;
  bool incomingBodyDirection_Strict = false;
  bool incomingBodyDirection_ForceLinearity = false;

  bool byIncomingBallDirection = false;
  blunted::Vector3 incomingBallDirection;

  bool byOutgoingBallDirection = false;
  blunted::Vector3 outgoingBallDirection;

  bool byTripType = false;
  int tripType = 0;

  SelectionProperties properties;
};

#endif
