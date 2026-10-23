#ifndef FOOTBALL_SIM_RUNTIME_RESTART_CONTEXT_HPP
#define FOOTBALL_SIM_RUNTIME_RESTART_CONTEXT_HPP

#include <optional>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/match/game_mode.hpp"
#include "football/player/player_definition.hpp"
#include "football/player/player_restart_facts.hpp"
#include "referee/restart_phase.hpp"

class Player;

namespace football::sim {

// Restart execution lifecycle, declared by the rules (referee/restart_phase.hpp).

// Targets are in the single Stadium Frame.
// Planning never moves actors, selects animations, or consumes RNG. The plan is
// long-lived, so it stores stable PlayerIds only: Runtime resolves them to live
// actors at execution time (see RestartRoster).
struct RestartPlayerTarget {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  football::math::Vector3 position;
};

// Data-only restart layout produced by the Runtime planner. It describes the
// legal spot and each actor's intended target; it never executes a mutation.
struct RestartPlan {
  e_GameMode mode = e_GameMode_Normal;
  int team_id = -1;
  football::player::PlayerId taker = football::player::kInvalidPlayerId;
  football::math::Vector3 ball_position;
  std::vector<RestartPlayerTarget> players;
};

struct RestartState {
  football::sim::Tick entered_tick{};
  football::sim::Tick earliest_restart_tick{};
  football::sim::Tick timeout_tick{};
  RestartPhase phase = RestartPhase::Pending;
  bool setup_done = false;
  bool used_timeout_placement = false;
  RestartPlan plan;
};

// Runtime-owned restart execution authority. The Referee only submits awards
// and rule verdicts; this is the single owner of the in-flight restart, its
// deadlines and its progress. It is not a rule fact and never mirrors the
// Referee's decision state.
struct RestartContext : football::player::PlayerRestartFacts {
  // Match-scoped identity, shared with RestartAwardedEvent/RestartTakenEvent.
  std::uint64_t id = 0;
  e_GameMode kind = e_GameMode_Normal;
  int team_id = -1;  // legacy team slot (0/1) taking the restart
  int setpiece_team = -1;
  football::sim::Tick stop_tick{};
  football::sim::Tick prepare_tick{};
  football::sim::Tick start_tick{};
  football::math::Vector3 position;  // Stadium Frame
  bool end_phase = false;
  std::optional<RestartState> state;

  // Live actor for the stable `taker` identity above, resolved by
  // RestartController. Runtime-only execution cache; never a rule/actor fact
  // and never serialized.
  Player* taker_actor = nullptr;

  void SetTaker(Player* actor);
  bool HasTaker() const { return taker_actor != nullptr; }

  bool NeedsSimulation() const {
    return active && state.has_value() && state->phase == RestartPhase::Pending;
  }
  bool Scheduled() const { return active && state.has_value(); }
  RestartPhase Phase() const {
    return state.has_value() ? state->phase : RestartPhase::InPlay;
  }
};

// Read-only target lookup used by observation and player control. The context
// owns the plan; callers never retain the returned value.
inline std::optional<football::math::Vector3> RestartTargetFor(
    const RestartContext& context, football::player::PlayerId player) {
  if (!context.NeedsSimulation()) return std::nullopt;
  for (const RestartPlayerTarget& target : context.state->plan.players) {
    if (target.player == player) return target.position;
  }
  return std::nullopt;
}

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_RESTART_CONTEXT_HPP
