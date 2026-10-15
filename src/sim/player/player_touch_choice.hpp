#ifndef FOOTBALL_SIM_PLAYER_TOUCH_CHOICE_HPP
#define FOOTBALL_SIM_PLAYER_TOUCH_CHOICE_HPP

#include <vector>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

// ANIM-1 A6.4 step 2c - the result of touch-animation selection, as one value.
//
// `Humanoid::GetBestCheatableAnimID` used to publish its answer through eight
// out-parameters, two of which no caller ever read. This struct replaces them so
// the selection has a single, inspectable result and the eventual Player-owned
// version of the loop can return it directly.
//
// `positions` is scratch as well as output: the function fills it with the root
// trajectory of each candidate it evaluates and leaves the chosen one in place.
// It is the one field the caller must not pre-populate.

namespace football::sim {

struct TouchChoice {
  std::vector<Vector3> positions;
  int touch_frame = -1;
  float radius_offset = 0.0f;
  Vector3 touch_position{0};
  Vector3 full_action_smuggle{0};
  Vector3 action_smuggle{0};
  radian rotation_smuggle = 0.0f;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_TOUCH_CHOICE_HPP
