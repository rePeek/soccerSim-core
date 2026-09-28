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

#ifndef _HPP_CORE_WORLD
#define _HPP_CORE_WORLD

class GameTask;  // legacy

// World is the future top-level simulation orchestrator.
//
// Phase 3 (current): only a simulation entry seam — it borrows the legacy
// GameTask and forwards Step() to it. Ownership of the authoritative
// WorldState and the systems that advance it (Physics / Action / Rules)
// migrate here in later phases.
class World {
public:
  // World borrows the legacy GameTask; it does not own it. The invariant
  // is: a World that exists always has a valid GameTask to step.
  explicit World(GameTask& game_task) : legacy_game_task_(game_task) {}

  // Advance the simulation by one phase (tick).
  void Step();

private:
  GameTask& legacy_game_task_;
};

#endif  // _HPP_CORE_WORLD