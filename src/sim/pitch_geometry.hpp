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

#ifndef FOOTBALL_SIM_MATCH_PITCH_GEOMETRY_HPP
#define FOOTBALL_SIM_MATCH_PITCH_GEOMETRY_HPP

#include "model/pitch.hpp"

// Transitional aliases for runtime animation/mechanics. Pitch is the single
// source of geometry; new match code should query its owned Pitch instead.
inline constexpr float pitchHalfW = football::model::MakeLegacyPitch().half_length();
inline constexpr float pitchHalfH = football::model::MakeLegacyPitch().half_width();
inline constexpr float pitchFullHalfW = football::model::MakeLegacyPitch().full_half_length();
inline constexpr float pitchFullHalfH = football::model::MakeLegacyPitch().full_half_width();
inline constexpr float lineHalfW = football::model::MakeLegacyPitch().line_half_width();

inline constexpr float goalDepth = football::model::MakeLegacyPitch().goal_depth();
inline constexpr float goalHeight = football::model::MakeLegacyPitch().goal_height();
inline constexpr float goalHalfWidth = football::model::MakeLegacyPitch().goal_half_width();

#endif  // FOOTBALL_SIM_MATCH_PITCH_GEOMETRY_HPP
