#ifndef FOOTBALL_TEST_DEFAULT_AI_FIXTURE_HPP
#define FOOTBALL_TEST_DEFAULT_AI_FIXTURE_HPP

#include "ai/default_ai.hpp"
#include "sim/simulation.hpp"

namespace football::test {
template<class T> concept HasDecisionObject = requires(T &actor) { actor.GetController(); };

// Diagnostic composition, explicitly outside Simulation. Production composition
// is GameEnv::step; tests independently replay that same value contract.
inline void StepDefaultAI(Simulation &simulation, const PlayerControlSet &overrides = {}) {
  const WorldState world = simulation.Observe();
  auto boards = simulation.ObserveTactics();
  ai::UpdateTactics(world, boards);
  PlayerControlSet controls;
  ai::DefaultAI{}.Update(world, boards, controls);
  for (const auto &control : overrides.controls()) controls.Set(control.player, control);
  simulation.Step(controls);
}
}  // namespace football::test

#endif
