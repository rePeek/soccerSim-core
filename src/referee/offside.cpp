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

#include "referee/offside.hpp"
#include "football/pitch/pitch.hpp"

#include <cmath>

#include "football/foundation/math/scalar.hpp"

namespace football::sim::rules {

float GetOffsideLine(std::span<const RefereePlayerFact> players,
                     const football::math::Vector3& ball_position,
                     const football::pitch::Pitch& pitch, int defending_team_id,
                     int defending_side) {
  const signed int side = defending_side;

  // Deepest defender of the defending team.
  int deepest = -1;
  for (int i = 0; i < static_cast<int>(players.size()); ++i) {
    if (players[i].team_id != defending_team_id) continue;
    if (deepest < 0 ||
        players[i].position.coords[0] * side >
            players[deepest].position.coords[0] * side) {
      deepest = i;
    }
  }

  // Offside is decided by the one-but-deepest defender; the legacy scan starts
  // its comparison from the origin, which is preserved here.
  football::math::Vector3 second_deepest;  // (0, 0, 0)
  for (int i = 0; i < static_cast<int>(players.size()); ++i) {
    if (players[i].team_id != defending_team_id || i == deepest) continue;
    if (players[i].position.coords[0] * side >
        second_deepest.coords[0] * side) {
      second_deepest = players[i].position;
    }
  }

  float offsideLine = second_deepest.coords[0];
  if (ball_position.coords[0] * side > offsideLine * side) {
    offsideLine = ball_position.coords[0];
  }
  if (offsideLine * side < 0) offsideLine = 0;
  offsideLine = football::math::clamp(offsideLine, -pitch.half_length(), pitch.half_length());

  return offsideLine;
}

}  // namespace football::sim::rules
