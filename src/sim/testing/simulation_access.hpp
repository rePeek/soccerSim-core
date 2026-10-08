#ifndef FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP
#define FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP

#include <stdexcept>
#include "sim/simulation.hpp"
#include "sim/match/match.hpp"
#include "sim/player/player_control_builder.hpp"

namespace football::sim::testing {

// Internal diagnostics only. Not exported by sim_contracts or used by actors.
// Product execution remains Init/Step/Observe/Finished/Result/Stop.
class SimulationAccess {
 public:
  static PlayerTickContext PlayerTickOf(Simulation& simulation, Player& actor) {
    return simulation.PlayerTickFacts(actor);
  }
  static MatchClock& ClockOf(Simulation& simulation) {
    if (!simulation.match_ || !simulation.clock_) throw std::logic_error("simulation has no match");
    return *simulation.clock_;
  }
  static void SendOff(Simulation& simulation, Player& actor) {
    const auto tick = PlayerTickOf(simulation, actor);
    actor.SendOff(tick.ball, tick.now, tick.rng);
  }
  static void Deactivate(Simulation& simulation, Player& actor) {
    const auto tick = PlayerTickOf(simulation, actor);
    actor.Deactivate(tick.ball, tick.now);
  }
  static PlayerCommandInputs CommandInputsOf(Simulation& simulation) {
    if (!simulation.match_) throw std::logic_error("simulation has no match");
    auto& match = *simulation.match_;
    return {*match.GetBall(), match.touches(), RulesOf(simulation).GetBuffer(),
            match.GetBallRetainer(), match.pitch()};
  }
  static BallTouchSink& EventsOf(Simulation& simulation) {
    if (!simulation.touch_sink_) throw std::logic_error("simulation has no match");
    return *simulation.touch_sink_;
  }
  static event::TouchState& TouchesOf(Simulation& simulation) {
    if (!simulation.match_) throw std::logic_error("simulation has no match");
    return simulation.match_->touches_;
  }
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
