#include "world.hpp"

#include "gametask.hpp"  // legacy

void World::Step() {
  // Phase 3: forward to the legacy simulation entry point. The WorldState +
  // systems design replaces this in later phases.
  legacy_game_task_.ProcessPhase();
}