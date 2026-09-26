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

#include "import_hierarchy.hpp"

namespace blunted {

ImportNode::ImportNode(std::string name, ImportNodeKind kind)
    : name(std::move(name)), kind(kind) {
  DO_VALIDATION;
  // The legacy defaults, kept exactly: Spatial's constructor left the local
  // rotation at SetAngleAxis(0, (0, 0, -1)) and the position at the origin.
  // The axis is not cosmetic -- sinf(0) * -1 is -0.0f, and that sign is
  // observable in the derived rotation and in digests.
  localPosition.Set(0, 0, 0);
  const Vector3 axis(0, 0, -1);
  localRotation.SetAngleAxis(0, axis);
}

void ImportNode::SetPosition(const Vector3& value,
                             bool update_derived_transforms) {
  DO_VALIDATION;
  localPosition = value;
  if (update_derived_transforms) UpdateDerivedTransforms();
}

void ImportNode::SetRotation(const Quaternion& value,
                             bool update_derived_transforms) {
  localRotation = value;
  if (update_derived_transforms) UpdateDerivedTransforms();
}

ImportNode* ImportNode::AddChild(std::unique_ptr<ImportNode> child) {
  DO_VALIDATION;
  child->parent = this;
  child->UpdateDerivedTransforms();
  children.push_back(std::move(child));
  return children.back().get();
}

void ImportNode::InvalidateDerivedTransforms() {
  DO_VALIDATION;
  derivedPositionDirty = true;
  derivedRotationDirty = true;
}

void ImportNode::UpdateDerivedTransforms() {
  DO_VALIDATION;
  InvalidateDerivedTransforms();
  for (auto& child : children) {
    DO_VALIDATION;
    child->UpdateDerivedTransforms();
  }
}

// Identical arithmetic to the old Spatial::GetDerivedPosition, minus the
// always-unit scale factor.
Vector3 ImportNode::GetDerivedPosition() const {
  if (derivedPositionDirty) {
    DO_VALIDATION;
    if (localMode == e_LocalMode_Relative) {
      DO_VALIDATION;
      if (parent) {
        DO_VALIDATION;
        const Quaternion parentDerivedRotation = parent->GetDerivedRotation();
        const Vector3 parentDerivedPosition = parent->GetDerivedPosition();

        cachedDerivedPosition.Set(parentDerivedRotation * localPosition);
        cachedDerivedPosition += parentDerivedPosition;
      } else {
        cachedDerivedPosition = localPosition;
      }
    } else {
      cachedDerivedPosition = localPosition;
    }
    derivedPositionDirty = false;
  }
  return cachedDerivedPosition;
}

Quaternion ImportNode::GetDerivedRotation() const {
  if (derivedRotationDirty) {
    DO_VALIDATION;
    if (localMode == e_LocalMode_Relative) {
      DO_VALIDATION;
      if (parent) {
        DO_VALIDATION;
        cachedDerivedRotation =
            (parent->GetDerivedRotation() * localRotation).GetNormalized();
      } else {
        cachedDerivedRotation = localRotation;
      }
    } else {
      cachedDerivedRotation = localRotation;
    }
    derivedRotationDirty = false;
  }
  return cachedDerivedRotation;
}

void ImportNode::CollectAnchors(std::vector<ImportNode*>& out) const {
  DO_VALIDATION;
  for (const auto& child : children) {
    if (child->kind == ImportNodeKind::Anchor) out.push_back(child.get());
  }
  for (const auto& child : children) {
    if (child->kind == ImportNodeKind::Node) child->CollectAnchors(out);
  }
}

}  // namespace blunted
