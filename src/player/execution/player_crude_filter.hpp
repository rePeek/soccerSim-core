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

#ifndef PLAYER_CRUDE_FILTER_HPP
#define PLAYER_CRUDE_FILTER_HPP

#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/quadrant.hpp"
#include "player/execution/player_crude_query.hpp"

// The Player's crude candidate filter.
//
// A line-for-line translation of the legacy AnimCollection::CrudeSelection; it
// reads only typed clip metadata. ACFG-4 moved it out of the Animation module:
// selecting candidates is a decision, and Animation is the shared clip resource.
// The bodies live in player_crude_filter.cpp.
//
// The metadata it reads is still the legacy-shaped schema (velocity buckets,
// body/ball directions, retain states), so its semantics are legacy by nature -
// that is behaviour, not dead code. What is gone is the *offline baker's*
// dependency on it: the recovery tool no longer re-proves the old selection.
namespace football::player {

class CrudeAnimationFilter {
 public:
  static void Select(const std::vector<AnimationClip>& clips,
                     const CrudeSelectionQuery& query, DataSet& data_set);

  // Deterministic quadrant table (velocity + angle) mirroring the legacy
  // AnimCollection constructor. Only velocity and angle are read.
  static Quadrant GetQuadrant(int id);
};

}  // namespace football::player

#endif  // PLAYER_CRUDE_FILTER_HPP
