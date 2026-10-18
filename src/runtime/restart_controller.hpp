#ifndef FOOTBALL_SIM_RUNTIME_RESTART_CONTROLLER_HPP
#define FOOTBALL_SIM_RUNTIME_RESTART_CONTROLLER_HPP

#include "football/foundation/math/rng.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/runtime/simulation_config.hpp"
#include "referee/referee_tick_facts.hpp"
#include "runtime/restart_context.hpp"
#include "runtime/restart_planner.hpp"

class Player;

namespace football::sim::rules { class RuleCommandSink; }
namespace football::sim::event {
struct AwardRestartRuling;
class EventLog;
}  // namespace football::sim::event

namespace football::sim {

// Runtime-owned restart execution. The Referee submits rule verdicts; this is
// the single authority that schedules, places, times out and releases a
// restart. It consumes the same immutable per-instant facts the rules use and
// performs every actor/ball write through the composition's command port.
class RestartController {
 public:
  // Rule-state side effects Runtime must publish back to the Referee.
  struct TickOutcome {
    bool ceremonial_setup = false;
    bool restart_taken = false;
  };

  // Competition lifecycle. The opening/half-time kickoff ceremony is a Runtime
  // procedure, not a rule decision.
  void BeginMatch(int kickoff_team_id, const football::math::Vector3& kickoff_position);
  void OnPeriodEnded(MatchPhase ended_phase, football::sim::Tick now,
                     const football::math::Vector3& kickoff_position, int kickoff_team_id);
  // Diagnostic/test boundary: forget the in-flight restart without executing.
  void Clear();

  // Commits one confirmed award ruling and publishes the confirmed event.
  void Award(const football::sim::event::AwardRestartRuling& ruling, football::sim::Tick now,
             football::sim::event::EventLog& events);
  // The rules confirmed a legal release; Runtime owns the transition and clock.
  void ConfirmRelease();

  // One-tick restart execution, run at the legacy pre-movement rule boundary.
  TickOutcome Advance(const football::sim::rules::RefereeTickFacts& facts,
                      const MatchOptions& options, football::math::Rng& rng,
                      football::sim::rules::RuleCommandSink& commands,
                      football::sim::event::EventLog& events,
                      const RestartPlanningTable& planning);

  const RestartContext& context() const { return context_; }
  bool NeedsSimulation() const { return context_.NeedsSimulation(); }
  bool InPostRestartGrace() const {
    return post_restart_relax_ > football::sim::TickSpan{};
  }

 private:
  void ProcessRestart(const football::sim::rules::RefereeTickFacts& facts,
                      const MatchOptions& options, football::math::Rng& rng,
                      football::sim::rules::RuleCommandSink& commands, TickOutcome& outcome,
                      const RestartPlanningTable& planning);
  void PrepareCeremonialKickOff(const football::sim::rules::RefereeTickFacts& facts,
                                const MatchOptions& options, football::math::Rng& rng,
                                football::sim::rules::RuleCommandSink& commands);
  // Decrements the post-restart line grace exactly once per executed instant.
  void TickPostRestartGrace();

  RestartContext context_;
  football::sim::TickSpan post_restart_relax_{};
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_RESTART_CONTROLLER_HPP
