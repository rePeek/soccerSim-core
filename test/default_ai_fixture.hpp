#ifndef FOOTBALL_TEST_DEFAULT_AI_FIXTURE_HPP
#define FOOTBALL_TEST_DEFAULT_AI_FIXTURE_HPP

#include <stdexcept>

#include "ai/default_ai.hpp"
#include "sim/testing/simulation_access.hpp"
#include "sim/simulation.hpp"
#include "sim/team/team.hpp"

namespace football::test {
template<class T> concept HasDecisionObject = requires(T &actor) { actor.GetController(); };

// Diagnostic composition owns its policy explicitly, just like GameEnv.
inline ai::DefaultAI MakeDefaultAI(const Simulation &simulation) {
  using football::sim::testing::SimulationAccess;
  return ai::DefaultAI(SimulationAccess::TeamOf(simulation, 0)->GetModel(),
                       SimulationAccess::TeamOf(simulation, 1)->GetModel(),
                       SimulationAccess::PitchOf(simulation));
}

inline void StepDefaultAI(Simulation &simulation, const ai::DefaultAI &policy,
                          const PlayerControlSet &overrides = {}) {
  const WorldState world = simulation.Observe();
  PlayerControlSet controls;
  policy.Update(world, controls);
  for (const auto &control : overrides.controls()) controls.Set(control.player, control);
  simulation.Step(controls);
}

// Functional fixtures must execute the real kickoff; empty controls cannot take it.
inline void TakeKickOff(Simulation &simulation) {
  const auto policy = MakeDefaultAI(simulation);
  for (int attempts = 0; !simulation.Observe().ball_in_play ||
                         simulation.Observe().in_set_piece; ++attempts) {
    if (attempts >= 1000 || simulation.Finished())
      throw std::runtime_error("fixture kickoff was not actually taken");
    StepDefaultAI(simulation, policy);
  }
}
}  // namespace football::test

#endif
