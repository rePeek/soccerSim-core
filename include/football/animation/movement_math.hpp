#ifndef FOOTBALL_ANIMATION_MOVEMENT_MATH_HPP
#define FOOTBALL_ANIMATION_MOVEMENT_MATH_HPP

// ANIM-1 A6.4 A8.0: pure reference-motion math shared by the animation
// selection path and the PlayerMotion action plan. No Player, Humanoid, Ball or
// RNG dependency: only vectors, the velocity bucket constants and an assert.

#include <cassert>
#include <span>
#include <vector>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::animation {

using football::math::Vector3;

// Movement (m/s) at an integer clip frame, in the same local space as the
// supplied positions. `smoothFrames` averages over a window around the frame.
inline Vector3 CalculateMovementAtFrame(std::span<const Vector3> positions,
                                        unsigned int frameNum,
                                        unsigned int smoothFrames = 0) {
  /*
    // simple version for debugging purposes
    unsigned int adaptedFrameNum = std::min(std::max(frameNum, (unsigned int)1),
    (unsigned int)positions.size() - 1); return (positions[adaptedFrameNum] -
    positions[adaptedFrameNum - 1]).Get2D() * 100.0f;
  */

  assert(frameNum < positions.size());

  // special case: want the exit movement to be unsmoothed, for we don't want the
  // wrong quantized velocity and such
  if (frameNum == positions.size() - 1) {
    return (positions[positions.size() - 1] - positions[positions.size() - 2]).Get2D() * 100.0f;
  }
  // special case: if we want the movement at frame 0, we can't get -1 to 0, we
  // need to get 0 to 1 instead
  if (frameNum == 0) {
    return (positions[1] - positions[0]).Get2D() * 100.0f;
  }

  Vector3 totalMovement;
  unsigned int count = 0;
  for (int frame = (signed int)(frameNum - smoothFrames);
       frame <= (signed int)(frameNum + smoothFrames); frame++) {
    if (frame > 0 && frame < (signed int)positions.size()) {
      // was: frame > 1 (i think that was a bug)
      totalMovement += (positions[frame] - positions[frame - 1]).Get2D() * 100.0f;
      count++;
    }
  }
  if (count > 0) {
    totalMovement /= (float)count;
  }

  return totalMovement;
}

// Maps a velocity from the animation's own bucket range onto the player's top
// speed, preserving the walk/idle part.
inline float StretchSprintTo(const float &inputVelocity,
                             float inputSpaceMaxVelocity,
                             float targetMaxVelocity) {
  assert(targetMaxVelocity > walkSprintSwitch);

  if (inputVelocity < walkSprintSwitch) return inputVelocity;

  float howMuchSprintage = inputVelocity - walkSprintSwitch;

  float oldLength = inputSpaceMaxVelocity - walkSprintSwitch;
  float newLength = targetMaxVelocity - walkSprintSwitch;

  float toNewFactor = newLength / oldLength;

  float resultSprintage = howMuchSprintage * toNewFactor;

  return walkSprintSwitch + resultSprintage;
}

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_MOVEMENT_MATH_HPP
