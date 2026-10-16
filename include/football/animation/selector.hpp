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

#ifndef _HPP_BAKED_SELECTOR
#define _HPP_BAKED_SELECTOR

#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/quadrant.hpp"
#include "football/animation/selection_query.hpp"

// Selection over baked AnimationClips. This is a line-for-line translation of
// the legacy AnimCollection::CrudeSelection; it reads only typed metadata.
//
// ANIM-2 A12.1: the bodies live in src/animation/selector.cpp, part of
// football::animation_base. The runtime and the offline baker share one
// definition without the baker linking the runtime animation archive.
class BakedAnimationSelector {
 public:
  static void CrudeSelection(const std::vector<AnimationClip>& clips,
                             const CrudeSelectionQuery& query,
                             DataSet& data_set);

  // Deterministic quadrant table (velocity + angle) mirroring the legacy
  // AnimCollection constructor. Only velocity and angle are read.
  static Quadrant GetQuadrant(int id);
};

#endif
