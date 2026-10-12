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

// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "foundation/geometry/triangle.hpp"

#include <cmath>

namespace blunted {

  bool Triangle::IntersectsLine(const Line &u_ray, Vector3 &intersectVec) const {
    Vector3    u, v;                // triangle vectors
    Vector3    dir, w0, w;          // ray vectors
    real       r, a, b;             // params to calc ray-plane intersect

    // get triangle edge vectors and plane normal
    u = vertices[1] - vertices[0];
    v = vertices[2] - vertices[0];

    dir = u_ray.GetVertex(1) - u_ray.GetVertex(0);             // ray direction vector
    w0 = u_ray.GetVertex(0) - vertices[0];
    a = -normals[0].GetDotProduct(w0);
    b = normals[0].GetDotProduct(dir);
    if (fabs(b) < 0.000001f) {
      // ray is parallel to triangle plane
      if (a == 0)                // ray lies in triangle plane
        return false; // 2
      else return false;             // ray disjoint from plane
    }

    // get intersect point of ray with triangle plane
    r = a / b;
    // hack to make edges look better, should be < 0 and > 1

    if (r <= 0.0 || r >= 1.0)                   // ray goes away from triangle
      return false;                  // => no intersect
    // for a segment, also test if (r > 1.0) => no intersect

    intersectVec = u_ray.GetVertex(0) + dir * r;           // intersect point of ray and plane

    // is I inside T?
    real uu, uv, vv, wu, wv, D;
    uu = u.GetDotProduct(u);
    uv = u.GetDotProduct(v);
    vv = v.GetDotProduct(v);
    w = intersectVec - vertices[0];
    wu = w.GetDotProduct(u);
    wv = w.GetDotProduct(v);
    D = uv * uv - uu * vv;

    // get and test parametric coords
    real s, t;
    s = (uv * wv - vv * wu) / D;
    if (s < 0.0 || s > 1.0)        // I is outside T
      return false;
    t = (uv * wu - uu * wv) / D;
    if (t < 0.0 || (s + t) > 1.0)  // I is outside T
      return false;

    return true;                      // I is in T
  }

}
