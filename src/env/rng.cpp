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

// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "env/rng.hpp"

#include "env/main.hpp"

namespace blunted {

  // Accessors, not algorithm: the draw math lives in foundation/math/rng.hpp.
  // These exist only because simulation code reaches the context RNG through
  // a global today.
  void randomseed(unsigned int seed) {
    DO_VALIDATION;
    GetContext().rng.Seed(seed);
    GetContext().rng_non_deterministic.Seed(seed);
  }

  real boostrandom(real min, real max) {
    DO_VALIDATION;
    // Diagnostic counter: presentation code drawing from the deterministic
    // RNG shows up here.
    GetContext().rng_draw_count++;
    return GetContext().rng.Uniform(min, max);
  }

  real random_non_determ(real min, real max) {
    DO_VALIDATION;
    return GetContext().rng_non_deterministic.Uniform(min, max);
  }

}
