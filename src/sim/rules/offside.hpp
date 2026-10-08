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

#include "foundation/time/tick.hpp"

namespace football::ball { class Ball; }
class MentalImage;

namespace football::sim::rules {

// Pure offside geometry from explicit observation facts: the defending team's id
// and dynamic side, the sampling instant and the current football::ball::Ball. Uses the
// second-deepest defender, ball and halfway line; prediction and geometry retain
// legacy semantics. No Match, Team or simulation-state access.
float GetOffsideLine(const MentalImage& mentalImage, football::sim::Tick now,
                     const football::ball::Ball& ball, int defending_team_id, int defending_side,
                     unsigned int futureSim_ms = 0);

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_OFFSIDE_HPP
