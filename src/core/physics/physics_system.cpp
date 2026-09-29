#include "physics_system.hpp"

#include <functional>

#include "core/physics/contact/player/player_contact.hpp"

namespace football_sim::physics {

BallPhysicsStepResult PhysicsSystem::Step(
    football_sim::World& world, float dt,
    const std::vector<PlayerBodyCandidate>& playerBodies) {
  BallPhysicsStepResult result =
      BallPhysics::Step(world.GetBall(), dt, true, goal_, playerBodies);

  std::vector<std::reference_wrapper<football_sim::Player>> players;
  players.reserve(world.Players().size());
  for (football_sim::Player& player : world.Players()) {
    players.push_back(player);
  }
  football_sim::contact::ResolvePlayerContactBatch(players);
  world.AdvanceTick();
  return result;
}

}  // namespace football_sim::physics
