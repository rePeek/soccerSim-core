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

#include "foundation/math/bluntmath.hpp"

#include "env/main.hpp"

namespace blunted {

  void randomseed(unsigned int seed) {
    DO_VALIDATION;
    GetContext().rng.engine().seed(seed);
    GetContext().rng_non_deterministic.engine().seed(seed);
  }

  inline real boostrandom() {
    DO_VALIDATION;
    GetContext().rng_draw_count++;
    return GetContext().rng();
  }

  real boostrandom(real min, real max) {
    DO_VALIDATION;
    float stretch = max - min;
    real value = min + (boostrandom() * stretch);
    return value;
  }

  real random_non_determ(real min, real max) {
    DO_VALIDATION;
    float stretch = max - min;
    real value = min + (GetContext().rng_non_deterministic() * stretch);
    return value;
  }

}
