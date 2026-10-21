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

#include <algorithm>
#include <cmath>
#include <limits>

#include "football/foundation/math/scalar.hpp"

namespace football::sim::rules {

namespace {

// The offside line is the second-last opponent, or the ball, or the halfway
// line, whichever is nearer to the goal the defending team attacks. All three
// are compared as projections along `side` so no world-axis sign is assumed.
float OffsideLineProjection(std::span<const RefereePlayerFact> players,
                            const football::math::Vector3& ball_position,
                            const football::pitch::Pitch& pitch,
                            int defending_team_id, int side) {
  // The two largest projections belong to the deepest and second-deepest
  // defender. An explicit two-value scan handles ties, a single defender and an
  // empty roster without a hidden origin fallback.
  float deepest = -std::numeric_limits<float>::infinity();
  float second = -std::numeric_limits<float>::infinity();
  for (const RefereePlayerFact& player : players) {
    if (player.team_id != defending_team_id) continue;
    const float projection = player.position.coords[0] * static_cast<float>(side);
    if (projection > deepest) {
      second = deepest;
      deepest = projection;
    } else if (projection > second) {
      second = projection;
    }
  }

  // Fewer than two opponents: the second-last opponent is undefined and the
  // halfway line is the conservative line.
  float line = std::isfinite(second) ? second : 0.0f;
  const float ball_projection = ball_position.coords[0] * static_cast<float>(side);
  line = std::max(line, ball_projection);
  // Never behind the halfway line, never past the goal line.
  line = football::math::clamp(line, 0.0f, pitch.half_length());
  return line;
}

}  // namespace

float GetOffsideLine(std::span<const RefereePlayerFact> players,
                     const football::math::Vector3& ball_position,
                     const football::pitch::Pitch& pitch, int defending_team_id,
                     int defending_side) {
  const int side = defending_side;
  const float projection =
      OffsideLineProjection(players, ball_position, pitch, defending_team_id, side);
  return static_cast<float>(side) * projection;
}

}  // namespace football::sim::rules
