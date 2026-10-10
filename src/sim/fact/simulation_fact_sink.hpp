#ifndef FOOTBALL_SIM_FACT_SIMULATION_FACT_SINK_HPP
#define FOOTBALL_SIM_FACT_SIMULATION_FACT_SINK_HPP

#include <utility>

#include "sim/fact/simulation_fact.hpp"

namespace football::sim {

// The write-only fact-production port for ball touches. Producers report
// immutable facts; the Simulation-owned buffer adds the tick, sequence and
// reset generation. Fouls no longer travel through here: the player contact
// solver reports FoulAssessment values directly.
class SimulationFactSink {
 public:
  virtual ~SimulationFactSink() = default;
  virtual void OnSimulationFact(event::SimulationFact fact) = 0;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_FACT_SIMULATION_FACT_SINK_HPP
