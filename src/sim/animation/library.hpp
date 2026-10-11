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

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "sim/animation/clip.hpp"

// Read-only view over a baked animations.simanim artifact.
//
// Header-only: the format reader is inlined so the offline baker can load and
// verify artifacts without linking the runtime animation archive. Only the
// header-only format (clip.hpp / simanim_format.hpp) is required.
class AnimationLibrary {
 public:
  bool Load(const std::filesystem::path& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) {
      std::cerr << "AnimationLibrary: cannot open " << path << "\n";
      return false;
    }

    std::uint64_t hash = UINT64_C(14695981039346656037);
    std::array<char, 16384> bytes;
    while (is.read(bytes.data(), bytes.size()) || is.gcount() != 0) {
      for (std::streamsize i = 0; i < is.gcount(); ++i) {
        hash ^= static_cast<unsigned char>(bytes[static_cast<std::size_t>(i)]);
        hash *= UINT64_C(1099511628211);
      }
    }
    if (!is.eof()) return false;
    is.clear();
    is.seekg(0);

    uint32_t clip_count = 0;
    if (!SimAnimReadHeader(is, clip_count)) {
      std::cerr << "AnimationLibrary: bad or unsupported header in " << path << "\n";
      return false;
    }

    clips_.clear();
    clips_.reserve(clip_count);
    for (uint32_t i = 0; i < clip_count; ++i) {
      clips_.push_back(AnimationClip::Deserialize(is, i));
    }
    content_hash_ = hash;
    return true;
  }

  const AnimationClip& Get(uint32_t id) const { return clips_.at(id); }

  const std::vector<AnimationClip>& Clips() const { return clips_; }

  std::size_t Size() const { return clips_.size(); }
  // Stable resource provenance for offline snapshot pose lookup.
  std::uint64_t ContentHash() const { return content_hash_; }

 private:
  std::vector<AnimationClip> clips_;
  std::uint64_t content_hash_ = 0;
};

#endif
