#ifndef FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP
#define FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP

#include <span>

#include "sim/rules/referee_tick_facts.hpp"

class Player;

namespace football::sim::rules {

// Temporary read-only query surface for one fact consumption. It is assembled
// at the consumption instant and never stored, so a delayed fact can be resolved
// against the state that is actually current when the referee sees it. It is not
// the AI-facing WorldState, which is a different (and more expensive) projection.
struct RefereeView {
  const RefereeTickFacts& tick;
  std::span<Player* const> all_active_players;
  bool offsides_enabled = false;
};

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP
