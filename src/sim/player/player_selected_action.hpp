#ifndef FOOTBALL_SIM_PLAYER_SELECTED_ACTION_HPP
#define FOOTBALL_SIM_PLAYER_SELECTED_ACTION_HPP

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::sim {

// ANIM-1 A6.3b.3 (first cut): the prepared result of an animation/action
// selection. It is a plain description of what was selected - clip, cursor,
// duration and contact timing - not a decision, so the install entry point
// never re-selects a clip or vetoes an interrupt. Future selection upgrades
// (e.g. a transition cost) extend this struct instead of the Player executor.
struct SelectedAction {
  AnimationId clip_id = -1;
  e_FunctionType function_type = e_FunctionType_None;
  int start_frame = 0;
  int frame_count = 0;
  int touch_frame = -1;
  Vector3 touch_position;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_SELECTED_ACTION_HPP
