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

#include "player/perception.hpp"

#include <cassert>
#include <cmath>
#include <vector>

#include "football/foundation/math/scalar.hpp"
#include "football/player/player_role.hpp"
#include "player/player_motion_constants.hpp"

using namespace football::math;

namespace football::sim::player {

float CalculateFreeSpace(Tick now, const PlayerObservation& observation,
                         std::chrono::milliseconds age, int teamID,
                         const Vector3 &focusPos, float safeDistance,
                         float futureTime_sec) {
  std::vector<PlayerImagePosition> opponentPlayerImages =
      observation.TeamPlayers(std::abs(teamID - 1), age, now);
  float currentSituation = 0.0f;

  // player position predictions
  for (int i = 0; i < (signed int)opponentPlayerImages.size(); i++) {
    if (opponentPlayerImages[i].player_role != e_PlayerRole_GK) {
      // resulting opp position
      opponentPlayerImages[i].position = opponentPlayerImages[i].position + opponentPlayerImages[i].movement * 0.2f; // slowness
      Vector3 toFocusMovement = (focusPos - opponentPlayerImages[i].position).GetNormalized(0) * playerSprintVelocity * clamp(futureTime_sec - 0.2f, 0.0f, 1000.0f);
      if (toFocusMovement.GetLength() > (focusPos - opponentPlayerImages[i].position).GetLength()) toFocusMovement = focusPos - opponentPlayerImages[i].position;
      opponentPlayerImages[i].position += toFocusMovement;

      float situation = 1.0f - clamp((opponentPlayerImages[i].position - focusPos).GetLength(), 0, safeDistance) / safeDistance;

      currentSituation += situation;
    }
  }

  return 1.0f - NormalizedClamp(currentSituation, 0.0f, 2.5f);
}

}  // namespace football::sim::player
