#ifndef FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP
#define FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP

#include <stdexcept>
#include "sim/simulation.hpp"
#include "sim/match/match.hpp"

namespace football::sim::testing {

// Internal diagnostics only. Not exported by sim_contracts or used by actors.
// Product execution remains Init/Step/Observe/Finished/Result/Stop.
class SimulationAccess {
 public:
  static Referee& RulesOf(Simulation& simulation) {
    if (!simulation.referee_) throw std::logic_error("simulation has no match");
    return *simulation.referee_;
  }
  static rules::RuleCommandSink& CommandsOf(Simulation& simulation) {
    if (!simulation.rule_commands_) throw std::logic_error("simulation has no match");
    return *simulation.rule_commands_;
  }
  static rules::RefereeTickFacts RefereeFactsOf(const Simulation& simulation) {
    return simulation.RefereeFacts();
  }
  static void ProcessRules(Simulation& simulation, Referee& referee) {
    referee.Process(RefereeFactsOf(simulation), simulation.match_->options(),
                    simulation.rng_, CommandsOf(simulation));
  }
};

} // namespace football::sim::testing
#endif
