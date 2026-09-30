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

#ifndef _HPP_ANIMATION_LIBRARY
#define _HPP_ANIMATION_LIBRARY

#include <cstdint>
#include <filesystem>
#include <vector>

#include "animation/clip.hpp"

// Read-only view over a baked animations.simanim artifact.
class AnimationLibrary {
 public:
  bool Load(const std::filesystem::path& path);

  const AnimationClip& Get(uint32_t id) const { return clips_.at(id); }

  const std::vector<AnimationClip>& Clips() const { return clips_; }

  std::size_t Size() const { return clips_.size(); }

 private:
  std::vector<AnimationClip> clips_;
};

#endif