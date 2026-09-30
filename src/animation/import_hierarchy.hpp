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

#ifndef _HPP_FOOTBALL_IMPORT_HIERARCHY
#define _HPP_FOOTBALL_IMPORT_HIERARCHY

// The CPU data model the animation import path needs.
//
// Before D4c2 this was the scene graph (Node/Object/Geometry/Spatial): a
// renderer-facing type system used, at import time, for four things only --
// names, hierarchy ownership, local transforms and derived transforms. This
// header keeps exactly those four, and nothing else. No mesh data, no
// materials, no renderer objects, no bounding volumes, no scene registration.
//
// Two details are deliberate, because they are observable:
//
//   * e_LocalMode is kept. It is not legacy scene decoration: the transform
//     math below branches on it, and the loader sets it.
//   * The derived transforms stay lazy (dirty flags + cached values) and the
//     arithmetic is copied verbatim from the old Spatial implementation. The
//     order in which transforms are recomputed is then identical, so the
//     import results stay bit-exact.
//
// Scale is deliberately absent: nothing on the import path ever called
// SetScale, and multiplying by a unit scale is exact in IEEE arithmetic.

#include "env/defines.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

#include <memory>
#include <string>
#include <vector>

namespace blunted {

enum e_LocalMode {
  e_LocalMode_Relative,
  e_LocalMode_Absolute
};

// The asset XML has two kinds of entries. <node> entries form the transform
// hierarchy; <geometry> entries are the named body-part anchors that the
// touch computation measures against (the old code modelled them as Geometry
// objects). The distinction survives because the import traversal visits a
// node's anchors before recursing into its child nodes, and that order decides
// ties when several anchors are equally close to the ball.
enum class ImportNodeKind {
  Node,
  Anchor
};

struct ImportNode {
  explicit ImportNode(std::string name,
                      ImportNodeKind kind = ImportNodeKind::Node);

  void SetName(const std::string& value) { name = value; }
  const std::string& GetName() const { return name; }
  void SetLocalMode(e_LocalMode value) { localMode = value; }

  // update_derived_transforms mirrors the legacy flag: the animation pose
  // writes local transforms without touching the caches and invalidates the
  // tree once at the end of Apply().
  void SetPosition(const Vector3& value, bool update_derived_transforms = true);
  Vector3 GetPosition() const { return localPosition; }
  void SetRotation(const Quaternion& value, bool update_derived_transforms = true);
  Quaternion GetRotation() const { return localRotation; }

  ImportNode* AddChild(std::unique_ptr<ImportNode> child);

  // Invalidates the cached derived transforms of this node and its whole
  // subtree. Was RecursiveUpdateSpatialData(); it never computed anything.
  void UpdateDerivedTransforms();

  Vector3 GetDerivedPosition() const;
  Quaternion GetDerivedRotation() const;

  // Body-part anchors in the legacy traversal order: this node's own anchors
  // first, then the child nodes depth-first.
  void CollectAnchors(std::vector<ImportNode*>& out) const;

  std::string name;
  ImportNodeKind kind = ImportNodeKind::Node;
  e_LocalMode localMode = e_LocalMode_Relative;
  Vector3 localPosition;
  Quaternion localRotation;
  ImportNode* parent = nullptr;
  std::vector<std::unique_ptr<ImportNode>> children;

 private:
  void InvalidateDerivedTransforms();

  mutable bool derivedPositionDirty = true;
  mutable bool derivedRotationDirty = true;
  mutable Vector3 cachedDerivedPosition;
  mutable Quaternion cachedDerivedRotation;
};

// Owns an imported hierarchy plus the body-part anchors the touch computation
// needs, collected once at load time instead of searched for on every use.
struct ImportHierarchy {
  std::unique_ptr<ImportNode> root;
  std::vector<ImportNode*> anchors;
};

}  // namespace blunted

#endif
