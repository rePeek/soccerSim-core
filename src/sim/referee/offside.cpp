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


#include "sim/referee/offside.hpp"

#include <cmath>

#include "foundation/math/scalar.hpp"
#include "sim/observation/mentalimage.hpp"

using football::ball::Ball;

namespace football::sim::rules {

float GetOffsideLine(const MentalImage& mentalImage, football::sim::Tick now,
                     const football::model::Pitch& pitch,
                     const Ball& ball, int defending_team_id, int defending_side,
                     unsigned int futureSim_ms) {
  signed int side = defending_side;

  auto opponentPlayerImages = mentalImage.GetTeamPlayerImages(defending_team_id, now);

  int dudDeepestOpponent = 0;
  Vector3 deepestOpponentPosition;

  for (int i = 0; i < (signed int)opponentPlayerImages.size(); i++) {

    opponentPlayerImages[i].position.coords[0] += opponentPlayerImages[i].movement.coords[0] * futureSim_ms * 0.001f;

    // for offside
    if (opponentPlayerImages[i].position.coords[0] * side >
        opponentPlayerImages.at(dudDeepestOpponent).position.coords[0] * side) {
      dudDeepestOpponent = i;
    }
  }

  // offside: we are actually looking for the one-but-deepest opponent (association football rule! EAT THAT, PES6!! :P)
  for (int i = 0; i < (signed int)opponentPlayerImages.size(); i++) {
    if (opponentPlayerImages[i].position.coords[0] * side >
            deepestOpponentPosition.coords[0] * side &&
        i != dudDeepestOpponent) {
      deepestOpponentPosition = opponentPlayerImages[i].position;
    }
  }

  float offsideLine = deepestOpponentPosition.coords[0];
  if (mentalImage.GetBallPrediction(0, now, ball).coords[0] * side > offsideLine * side) {
    offsideLine = mentalImage.GetBallPrediction(0, now, ball).coords[0];
  }
  if (offsideLine * side < 0) offsideLine = 0;
  offsideLine = clamp(offsideLine, -pitch.half_length(), pitch.half_length());

  return offsideLine;
}

}  // namespace football::sim::rules
