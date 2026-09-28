// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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