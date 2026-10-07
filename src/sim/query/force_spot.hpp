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

#ifndef FOOTBALL_SIM_QUERY_FORCE_SPOT_HPP
#define FOOTBALL_SIM_QUERY_FORCE_SPOT_HPP

#include "foundation/math/vector3.hpp"

using namespace blunted;

enum e_DecayType {
  e_DecayType_Constant,
  e_DecayType_Variable
};

enum e_MagnetType {
  e_MagnetType_Attract,
  e_MagnetType_Repel
};

// forcefields consist of forcespots, representing a repelling or attracting force from a position, including linearity/etc parameters
struct ForceSpot {
  Vector3 origin;
  e_MagnetType magnetType;
  e_DecayType decayType;
  float exp = 1.0f;
  float power = 0.0f;
  float scale = 0.0f; // scaled #meters until effect is almost decimated
};

#endif  // FOOTBALL_SIM_QUERY_FORCE_SPOT_HPP
