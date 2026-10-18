#ifndef FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP
#define FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP

#include "referee/referee_tick_facts.hpp"

namespace football::sim::rules {

// Read-only value surface for one fact consumption. It is assembled at the
// consumption instant and never stored. It carries only the sampled rule facts
// the boundary functions need; the accepted-touch path uses BallTouchFacts.
struct RefereeView {
  const RefereeTickFacts& tick;
  bool post_restart_grace = false;
};

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP
