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

#ifndef _HPP_BODY_COLLISION
#define _HPP_BODY_COLLISION

#include <string>
#include <vector>

#include "../../base/geometry/aabb.hpp"

// Snapshot of the collision-relevant body pose. It intentionally contains no
// scene objects: Match may consume this state without traversing Scene3D.
struct BodyCollider {
  std::string name;
  blunted::AABB bounds;
  blunted::Vector3 anchor;
};

class BodyCollisionState {
 public:
  void Clear() { colliders.clear(); }

  void Add(const std::string &name, const blunted::AABB &bounds,
           const blunted::Vector3 &anchor) {
    colliders.push_back({name, bounds, anchor});
  }

  const std::vector<BodyCollider> &GetColliders() const { return colliders; }

  void Mirror() {
    for (auto &collider : colliders) {
      const blunted::Vector3 min = collider.bounds.minxyz;
      const blunted::Vector3 max = collider.bounds.maxxyz;
      collider.bounds.minxyz.coords[0] = -max.coords[0];
      collider.bounds.minxyz.coords[1] = -max.coords[1];
      collider.bounds.maxxyz.coords[0] = -min.coords[0];
      collider.bounds.maxxyz.coords[1] = -min.coords[1];
      collider.bounds.MakeDirty();
      collider.anchor.Mirror();
    }
  }

 private:
  std::vector<BodyCollider> colliders;
};

#endif
