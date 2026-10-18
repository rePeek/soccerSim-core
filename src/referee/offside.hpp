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

#ifndef FOOTBALL_SIM_RULES_OFFSIDE_HPP
#define FOOTBALL_SIM_RULES_OFFSIDE_HPP

#include <span>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "referee/referee_player_fact.hpp"

namespace football::sim::rules {

// Pure offside geometry from explicit value facts: the defending team's id and
// dynamic side, the current ball position and the sampled active players. Uses
// the second-deepest defender, the ball and the halfway line. No MentalImage,
// Player or simulation-state access.
float GetOffsideLine(std::span<const RefereePlayerFact> players,
                     const football::math::Vector3& ball_position,
                     const football::pitch::Pitch& pitch,
                     int defending_team_id, int defending_side);

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_OFFSIDE_HPP
