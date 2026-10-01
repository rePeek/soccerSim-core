#ifndef FOOTBALL_CONTROL_COACH_COACH_CONTROL_CONTEXT_HPP
#define FOOTBALL_CONTROL_COACH_COACH_CONTROL_CONTEXT_HPP

#include "control/control_ids.hpp"
#include "control/world_state_view.hpp"

struct CoachControlContext {
  TeamId self = kInvalidTeamId;
  const WorldStateView& world;
};

#endif  // FOOTBALL_CONTROL_COACH_COACH_CONTROL_CONTEXT_HPP
