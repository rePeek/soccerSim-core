#ifndef FOOTBALL_AI_ELIZA_KEEPER_HPP
#define FOOTBALL_AI_ELIZA_KEEPER_HPP

#include "eliza_parameters.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai::eliza {

// The keeper's intent for one tick. Hands legality (backpass, box, retainer)
// stays with Rules and the Player's BuildPlayerCommands; this only expresses
// what the keeper wants to do.
struct KeeperDecision {
  football::math::Vector3 target;
  bool rush = false;
  bool save = false;
};

// Keeper positioning on the ball-goal line, a rush decision for a loose ball in
// the box, and a save intent.
KeeperDecision DecideKeeper(const WorldState& world, const WorldPlayerState& self,
                            const ElizaParameters& parameters);

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_KEEPER_HPP
