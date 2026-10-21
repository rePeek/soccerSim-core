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

#include "referee/goal.hpp"
#include "football/pitch/pitch.hpp"

#include <cmath>

namespace football::sim {
using football::math::Vector3;

bool CrossedGoalLine(const football::pitch::Pitch& pitch, int side,
                     const Vector3& previous, const Vector3& current,
                     float ball_radius) {
  // The goal mouth is the plane x = goal_x clipped to the goal rectangle
  // (|y| <= goal half width, 0 <= z <= goal height). The whole ball must cross
  // the goal line, so the plane and the side-net veto are offset by the radius.
  const float goal_x =
      (pitch.half_length() + pitch.line_half_width() + ball_radius) * side;
  const float half_goal_width = pitch.goal_half_width();
  const float goal_height = pitch.goal_height();

  // Extra check: ball could have gone 'in' via the side netting, if line begin
  // == inside pitch, but outside of post, and line end == in goal. disallow!
  if (fabs(previous.coords[1]) > half_goal_width &&
      fabs(previous.coords[0]) >
          pitch.half_length() - pitch.line_half_width() - ball_radius) {
    return false;
  }

  const float dx = current.coords[0] - previous.coords[0];
  if (std::fabs(dx) < 0.000001f) return false;  // parallel to the goal plane
  const float r = (goal_x - previous.coords[0]) / dx;
  if (r <= 0.0f || r >= 1.0f) return false;  // crossing strictly inside the step

  const float y = previous.coords[1] + (current.coords[1] - previous.coords[1]) * r;
  const float z = previous.coords[2] + (current.coords[2] - previous.coords[2]) * r;
  if (std::fabs(y) > half_goal_width || z < 0.0f || z > goal_height) return false;

  return true;
}

}  // namespace football::sim
