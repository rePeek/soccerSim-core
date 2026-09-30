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

#include <cstdint>
#include <vector>

#include "animation/animcollection.hpp"  // CrudeSelectionQuery
#include "animation/clip.hpp"

// Selection over baked AnimationClips. This is a line-for-line translation of
// the legacy AnimCollection::CrudeSelection; it reads only typed metadata.
class BakedAnimationSelector {
 public:
  static void CrudeSelection(const std::vector<AnimationClip>& clips,
                             const CrudeSelectionQuery& query,
                             std::vector<uint32_t>& data_set);
};

#endif