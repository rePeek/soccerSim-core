#ifndef FOOTBALL_CORE_PHYSICS_PHYSICS_SYSTEM_HPP
#define FOOTBALL_CORE_PHYSICS_PHYSICS_SYSTEM_HPP

#include <vector>

#include "core/model/world.hpp"
#include "core/physics/ball_physics.hpp"

namespace football_sim::physics {

// Deterministic headless tick orchestrator. World owns every mutable model;
// PhysicsSystem owns no simulation state and receives only tick-local contact
// candidates captured by the caller.
class PhysicsSystem {
 public:
  explicit PhysicsSystem(GoalGeometry goal = {}) : goal_(goal) {}

  BallPhysicsStepResult Step(
      football_sim::World& world, float dt,
      const std::vector<PlayerBodyCandidate>& playerBodies = {});

 private:
  GoalGeometry goal_;
};

}  // namespace football_sim::physics
#endif  // FOOTBALL_CORE_PHYSICS_PHYSICS_SYSTEM_HPP
