#include "physics_system.hpp"

#include <functional>

#include "core/contact/player_contact.hpp"

BallPhysicsStepResult PhysicsSystem::Step(
    football::model::World& world, float dt,
    const std::vector<PlayerBodyCandidate>& playerBodies) {
  BallPhysicsStepResult result =
      BallPhysics::Step(world.GetBall(), dt, true, goal_, playerBodies);

  std::vector<std::reference_wrapper<football::model::Player>> players;
  players.reserve(world.Players().size());
  for (football::model::Player& player : world.Players()) {
    players.push_back(player);
  }
  football::contact::ResolvePlayerContactBatch(players);
  world.AdvanceTick();
  return result;
}
