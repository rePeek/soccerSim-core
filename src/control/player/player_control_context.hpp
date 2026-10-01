#ifndef FOOTBALL_CONTROL_PLAYER_PLAYER_CONTROL_CONTEXT_HPP
#define FOOTBALL_CONTROL_PLAYER_PLAYER_CONTROL_CONTEXT_HPP

#include "control/coach/team_plan.hpp"
#include "control/control_ids.hpp"
#include "control/world_state_view.hpp"

struct PlayerControlContext {
  PlayerId self = kInvalidPlayerId;
  const WorldStateView& world;
  const TeamPlan& team_plan;
};

#endif  // FOOTBALL_CONTROL_PLAYER_PLAYER_CONTROL_CONTEXT_HPP
