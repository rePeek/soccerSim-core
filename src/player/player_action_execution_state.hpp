// ANIM-1 A9.4: the legacy per-action execution state.
//
// `Anim` (and the `RotationSmuggle` pair it contains) used to be declared in
// HumanoidBase's header. The Player now owns this storage; the struct lives
// here so the touch/selection code can name it without a Humanoid header.
#ifndef FOOTBALL_SIM_PLAYER_ACTION_EXECUTION_STATE_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_EXECUTION_STATE_HPP

#include <vector>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

#include "player/player_command.hpp"
#include "player/player_interrupt.hpp"

using namespace football::math;

struct RotationSmuggle {
  RotationSmuggle() {
    begin = 0;
    end = 0;
  }
  void operator = (const float &value) {
    begin = value;
    end = value;
  }
  radian begin;
  radian end;
};

struct Anim {
  AnimationId animationId = 0;
  int frameNum = 0;
  e_FunctionType functionType = e_FunctionType_None;
  Vector3 actionSmuggle;
  Vector3 actionSmuggleOffset;
  Vector3 actionSmuggleSustain;
  Vector3 actionSmuggleSustainOffset;
  Vector3 movementSmuggle;
  Vector3 movementSmuggleOffset;
  RotationSmuggle rotationSmuggle;
  radian rotationSmuggleOffset = 0;
  signed int touchFrame = -1;
  Vector3 touchPos;
  Vector3 incomingMovement;
  Vector3 outgoingMovement;
  Vector3 positionOffset;
  PlayerCommand originatingCommand;
  std::vector<Vector3> positions;
};

#endif  // FOOTBALL_SIM_PLAYER_ACTION_EXECUTION_STATE_HPP
