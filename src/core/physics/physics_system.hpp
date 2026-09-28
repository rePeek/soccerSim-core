#ifndef _HPP_CORE_PHYSICS_PHYSICS_SYSTEM
#define _HPP_CORE_PHYSICS_PHYSICS_SYSTEM

struct WorldState;

// Deterministic simulation system: advances the authoritative WorldState by
// one tick. No ownership, no hidden mutable state, and no reads of global
// Match/GameEnv/singletons.
//
// Phase 7A: empty shell. It does not run yet — World::Step still forwards to
// the legacy GameTask. The shell only fixes the dependency direction:
// core/state <- core/physics <- core/world.
class PhysicsSystem {
public:
  void Step(WorldState& state, float dt);
};

#endif  // _HPP_CORE_PHYSICS_PHYSICS_SYSTEM