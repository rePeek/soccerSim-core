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

#ifndef _HPP_ANIMATION_TYPES
#define _HPP_ANIMATION_TYPES

#include <cstdint>
#include "foundation/defines.hpp"
#include "foundation/math/bluntmath.hpp"

// Runtime identity of an entry in AnimationLibrary. Keep this signed while
// selection still uses -1 as its invalid sentinel.
using AnimationId = int32_t;
static_assert(sizeof(AnimationId) == sizeof(signed int));

// Animation-domain movement buckets and action kinds. These live here (not in
// sim/) so that animation stays free of any sim/ dependency: sim depends on
// these types through animation/.

const float idleVelocity = 0.0f;
const float dribbleVelocity = 3.5f;
const float walkVelocity = 5.0f;
const float sprintVelocity = 8.0f;

const float idleDribbleSwitch = 1.8f;
const float dribbleWalkSwitch = 4.2f;
const float walkSprintSwitch = 6.0f;

enum e_Velocity {
  e_Velocity_Idle,
  e_Velocity_Dribble,
  e_Velocity_Walk,
  e_Velocity_Sprint
};

enum e_FunctionType {
  e_FunctionType_None,
  e_FunctionType_Movement,
  e_FunctionType_BallControl,
  e_FunctionType_Trap,
  e_FunctionType_ShortPass,
  e_FunctionType_LongPass,
  e_FunctionType_HighPass,
  e_FunctionType_Header,
  e_FunctionType_Shot,
  e_FunctionType_Deflect,
  e_FunctionType_Catch,
  e_FunctionType_Interfere,
  e_FunctionType_Trip,
  e_FunctionType_Sliding,
  e_FunctionType_Special
};

// Stable numeric action tags written to .simanim metadata. These are shared by
// the offline source parser and baked runtime selection.
enum e_DefString {
  e_DefString_Empty = 0,
  e_DefString_OutgoingSpecialState = 1,
  e_DefString_IncomingSpecialState = 2,
  e_DefString_SpecialVar1 = 3,
  e_DefString_SpecialVar2 = 4,
  e_DefString_Type = 5,
  e_DefString_Trap = 6,
  e_DefString_Deflect = 7,
  e_DefString_Interfere = 8,
  e_DefString_Trip = 9,
  e_DefString_ShortPass = 10,
  e_DefString_LongPass = 11,
  e_DefString_Shot = 12,
  e_DefString_Sliding = 13,
  e_DefString_Movement = 14,
  e_DefString_Special = 15,
  e_DefString_BallControl = 16,
  e_DefString_HighPass = 17,
  e_DefString_Catch = 18,
  e_DefString_OutgoingRetainState = 19,
  e_DefString_IncomingRetainState = 20,
  e_DefString_Size = 21
};

inline e_FunctionType StringToFunctionType(e_DefString fun) {
  DO_VALIDATION;
  if (fun == e_DefString_Movement) return e_FunctionType_Movement;
  if (fun == e_DefString_BallControl) return e_FunctionType_BallControl;
  if (fun == e_DefString_Trap) return e_FunctionType_Trap;
  if (fun == e_DefString_ShortPass) return e_FunctionType_ShortPass;
  if (fun == e_DefString_LongPass) return e_FunctionType_LongPass;
  if (fun == e_DefString_HighPass) return e_FunctionType_HighPass;
  if (fun == e_DefString_Shot) return e_FunctionType_Shot;
  if (fun == e_DefString_Deflect) return e_FunctionType_Deflect;
  if (fun == e_DefString_Catch) return e_FunctionType_Catch;
  if (fun == e_DefString_Interfere) return e_FunctionType_Interfere;
  if (fun == e_DefString_Trip) return e_FunctionType_Trip;
  if (fun == e_DefString_Sliding) return e_FunctionType_Sliding;
  if (fun == e_DefString_Special) return e_FunctionType_Special;
  return e_FunctionType_None;
}

enum e_Foot {
  e_Foot_Left,
  e_Foot_Right
};

// Stable pose-array order shared by legacy source assets and baked clips.
enum BodyPart {
  middle,
  neck,
  left_thigh,
  right_thigh,
  left_knee,
  right_knee,
  left_ankle,
  right_ankle,
  left_shoulder,
  right_shoulder,
  left_elbow,
  right_elbow,
  body,
  player,
  body_part_max
};

typedef std::vector<int> DataSet;

inline int GetVelocityID(e_Velocity velo, bool treatDribbleAsWalk = false) {
  DO_VALIDATION;
  int id = 0;
  switch (velo) {
    DO_VALIDATION;
    case e_Velocity_Idle:
      id = 0;
      break;
    case e_Velocity_Dribble:
      id = 1;
      break;
    case e_Velocity_Walk:
      id = 2;
      break;
    case e_Velocity_Sprint:
      id = 3;
      break;
    default:
      id = 0;
      break;
  }
  if (treatDribbleAsWalk && id > 1) id--;
  return id;
}

inline float RangeVelocity(float velocity) {
  DO_VALIDATION;
  float ret_velocity = idleVelocity;
  if (velocity >= idleDribbleSwitch && velocity < dribbleWalkSwitch)
    ret_velocity = dribbleVelocity;
  else if (velocity >= dribbleWalkSwitch && velocity < walkSprintSwitch)
    ret_velocity = walkVelocity;
  else if (velocity >= walkSprintSwitch)
    ret_velocity = sprintVelocity;
  return ret_velocity;
}

inline float ClampVelocity(float velocity) {
  DO_VALIDATION;
  if (velocity < 0) return 0;
  if (velocity > sprintVelocity) return sprintVelocity;
  return velocity;
}

inline float EnumToFloatVelocity(e_Velocity velocity) {
  DO_VALIDATION;
  switch (velocity) {
    case e_Velocity_Idle: return idleVelocity;
    case e_Velocity_Dribble: return dribbleVelocity;
    case e_Velocity_Walk: return walkVelocity;
    case e_Velocity_Sprint: return sprintVelocity;
  }
  return 0;
}

inline e_Velocity FloatToEnumVelocity(float velocity) {
  DO_VALIDATION;
  const float ranged_velocity = RangeVelocity(velocity);
  if (ranged_velocity == idleVelocity) return e_Velocity_Idle;
  if (ranged_velocity == dribbleVelocity) return e_Velocity_Dribble;
  if (ranged_velocity == walkVelocity) return e_Velocity_Walk;
  if (ranged_velocity == sprintVelocity) return e_Velocity_Sprint;
  return e_Velocity_Idle;
}

#endif