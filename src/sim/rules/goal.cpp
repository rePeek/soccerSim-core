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

#include "sim/rules/goal.hpp"

#include <cmath>

#include "foundation/geometry/line.hpp"
#include "foundation/geometry/triangle.hpp"

namespace football::sim {
using blunted::Line;
using blunted::Triangle;
using blunted::Vector3;

bool CrossedGoalLine(const football::model::Pitch& pitch, int side,
                     const Vector3& previous, const Vector3& current) {
  Line line;
  line.SetVertex(0, previous);
  line.SetVertex(1, current);

  const float goal_x =
      (pitch.half_length() + pitch.line_half_width() + 0.11f) * side;
  const float half_goal_width = pitch.goal_half_width();
  const float goal_height = pitch.goal_height();
  Triangle goal1;
  goal1.SetVertex(0, Vector3(goal_x, half_goal_width, 0));
  goal1.SetVertex(1, Vector3(goal_x, -half_goal_width, 0));
  goal1.SetVertex(2, Vector3(goal_x, half_goal_width, goal_height));
  goal1.SetNormals(Vector3(-side, 0, 0));
  Triangle goal2;
  goal2.SetVertex(0, Vector3(goal_x, -half_goal_width, 0));
  goal2.SetVertex(1, Vector3(goal_x, -half_goal_width, goal_height));
  goal2.SetVertex(2, Vector3(goal_x, half_goal_width, goal_height));
  goal2.SetNormals(Vector3(-side, 0, 0));

  Vector3 intersectVec;
  bool intersect1 = goal1.IntersectsLine(line, intersectVec);
  bool intersect2 = goal2.IntersectsLine(line, intersectVec);
  // extra check: ball could have gone 'in' via the side netting, if line begin
  // == inside pitch, but outside of post, and line end == in goal. disallow!
  if (fabs(previous.coords[1]) > 3.7 &&
      fabs(previous.coords[0]) >
          pitch.half_length() - pitch.line_half_width() - 0.11) {
    return false;
  }
  if (intersect1 || intersect2) {
    return true;
  }
  return false;
}

}  // namespace football::sim
