#ifndef FOOTBALL_SIM_FACT_SIMULATION_FACT_SINK_HPP
#define FOOTBALL_SIM_FACT_SIMULATION_FACT_SINK_HPP

#include <utility>

#include "sim/fact/simulation_fact.hpp"

namespace football::sim {

// The single write-only fact-production port for physics and actors. Producers
// report immutable facts; the Simulation-owned buffer adds the tick, sequence
// and reset generation. There is no separate touch/trip protocol: every contact
// consequence travels through this one boundary.
class SimulationFactSink {
 public:
  virtual ~SimulationFactSink() = default;
  virtual void OnSimulationFact(event::SimulationFact fact) = 0;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_FACT_SIMULATION_FACT_SINK_HPP
