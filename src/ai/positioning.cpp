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


#include "ai/positioning.hpp"

#include <cmath>

namespace football::ai {

Vector3 GetForceFieldMovement(const std::vector<ForceSpot> &forceField,
                                 const Vector3 &currentPos,
                                 float attractorDampingDistance) {

  // attractorDampingDistance: from this distance to attractor, dampen influence so we won't overshoot target

  Vector3 cumulVec;
  float cumulForce = 0.0f;

  for (unsigned int i = 0; i < forceField.size(); i++) {

    const ForceSpot &forceSpot = forceField[i];

    float distance, intensity;

    distance = (forceSpot.origin - currentPos).GetLength();
    if (forceSpot.decayType == e_DecayType_Constant) {
      intensity = 1.0f;
    } else {
      intensity = clamp(1.0f - distance / forceSpot.scale, 0.0f, 1.0f);
      if (forceSpot.exp != 1.0f) intensity = std::pow(intensity, forceSpot.exp);
    }
    if (intensity > 0.0f) {
      Vector3 relativeOrigin = forceSpot.origin - currentPos;
      relativeOrigin.Normalize(0);
      if (forceSpot.magnetType == e_MagnetType_Repel) {
        relativeOrigin = -relativeOrigin;//currentPos - forceSpot.origin;
      } else {
        // attractors need damping
        if (distance < attractorDampingDistance) relativeOrigin *= distance / attractorDampingDistance;
      }

      float force = forceSpot.power * intensity;

      cumulVec += relativeOrigin * force;
      cumulForce += force;
    }
  }

  if (cumulForce == 0.0f) return 0; else return (cumulVec / cumulForce) * sprintVelocity;
}

}  // namespace football::ai
