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

#ifndef _HPP_BASE_GEOMETRY_TRIANGLE
#define _HPP_BASE_GEOMETRY_TRIANGLE

#include "foundation/math/vector3.hpp"
#include "foundation/geometry/line.hpp"

namespace blunted {

  // Triangle as used by goal-line crossing: three vertices plus one normal per
  // vertex. The legacy mesh state (texture coordinates, tangents, bitangents)
  // had no consumer and was removed.
  class Triangle {

    public:
      Triangle() = default;

      inline void SetVertex(unsigned char pos, const Vector &vec) {
        vertices[pos].coords[0] = vec.coords[0];
        vertices[pos].coords[1] = vec.coords[1];
        vertices[pos].coords[2] = vec.coords[2];
      }

      inline void SetNormals(const Vector &vec) {
        for (int i = 0; i < 3; i++) {
          normals[i].coords[0] = vec.coords[0];
          normals[i].coords[1] = vec.coords[1];
          normals[i].coords[2] = vec.coords[2];
        }
      }

      bool IntersectsLine(const Line &u_ray, Vector3 &intersectVec) const;

    protected:
      Vector3 vertices[3];
      Vector3 normals[3];

  };

}

#endif
