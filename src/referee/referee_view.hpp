#ifndef FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP
#define FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP

#include <span>

#include "referee/referee_tick_facts.hpp"
#include "referee/referee_player_fact.hpp"
#include "runtime/restart_context.hpp"


namespace football::sim::rules {

// Temporary read-only query surface for one fact consumption. It is assembled
// at the consumption instant and never stored, so a delayed fact can be resolved
// against the state that is actually current when the referee sees it. It is not
// the AI-facing WorldState, which is a different (and more expensive) projection.
struct RefereeView {
  const RefereeTickFacts& tick;
  std::span<const RefereePlayerFact> all_active_players;
  bool offsides_enabled = false;
  // Runtime-owned restart execution facts, borrowed read-only for this call.
  const football::sim::RestartContext* restart = nullptr;
  bool post_restart_grace = false;
};

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_REFEREE_VIEW_HPP
