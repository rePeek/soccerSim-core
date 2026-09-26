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

#include "import_loader.hpp"

#include "../../../base/utils.hpp"
#include "../../../main.hpp"
#include "../../../scene/resources/geometrydata.hpp"
#include "../../../types/resource.hpp"

namespace blunted {

ImportHierarchy ImportLoader::LoadObject(const std::string& filename,
                                         const Vector3& offset) const {
  XMLLoader loader;
  const XMLTree objectTree = loader.LoadFile(filename);

  ImportHierarchy hierarchy;
  hierarchy.root =
      LoadNode(filename, objectTree.children.begin()->second, offset);
  // Legacy traversal order: a node's own anchors first, then the child nodes
  // depth-first. The old Node::GetObjects() bundled objects before child nodes,
  // so this keeps the tie-breaking order independent of how the asset happens
  // to interleave <geometry> and <node> elements.
  hierarchy.root->CollectAnchors(hierarchy.anchors);
  return hierarchy;
}

std::unique_ptr<ImportNode> ImportLoader::LoadNode(
    const std::string& nodename, const XMLTree& objectTree,
    const Vector3& offset) const {
  std::unique_ptr<ImportNode> node(new ImportNode("objectnode: " + nodename));

  const std::string dirpart = nodename.substr(0, nodename.find_last_of('/') + 1);

  Vector3 position;
  Quaternion rotation;

  map_XMLTree::const_iterator objectIter = objectTree.children.begin();
  while (objectIter != objectTree.children.end()) {
    DO_VALIDATION;
    std::string objectName;
    Properties properties;
    e_LocalMode localMode = e_LocalMode_Relative;

    // NODE (recurse)

    if (objectIter->first == "node") {
      DO_VALIDATION;
      node->AddChild(LoadNode(dirpart, objectIter->second, offset));
    }

    else if (objectIter->first == "name") {
      DO_VALIDATION;
      objectName = objectIter->second.value;
      node->SetName(objectName);
    }

    else if (objectIter->first == "position") {
      DO_VALIDATION;
      position = GetVectorFromString(objectIter->second.value) + offset;
      node->SetPosition(position);
    }

    else if (objectIter->first == "rotation") {
      DO_VALIDATION;
      rotation = GetQuaternionFromString(objectIter->second.value);
      node->SetRotation(rotation);
    }

    // GEOMETRY (a named body-part anchor)

    else if (objectIter->first == "geometry") {
      DO_VALIDATION;
      std::string aseFilename;
      Vector3 position;
      Quaternion rotation;

      map_XMLTree::const_iterator iter = objectIter->second.children.begin();
      while (iter != objectIter->second.children.end()) {
        DO_VALIDATION;

        if (iter->first == "filename") {
          DO_VALIDATION;
          aseFilename = iter->second.value;
        }
        if (iter->first == "name") {
          DO_VALIDATION;
          objectName = iter->second.value;
        }
        if (iter->first == "position") {
          DO_VALIDATION;
          position = GetVectorFromString(iter->second.value) + offset;
        }
        if (iter->first == "rotation") {
          DO_VALIDATION;
          rotation = GetQuaternionFromString(iter->second.value);
        }
        if (iter->first == "properties") {
          DO_VALIDATION;
          InterpretProperties(iter->second.children, properties);
        }
        if (iter->first == "localmode") {
          DO_VALIDATION;
          localMode = InterpretLocalMode(iter->second.value);
        }

        iter++;
      }

      // The mesh itself is still loaded, because dropping the import is a
      // separate change (D4c3) from replacing the hierarchy representation
      // (this one). Nothing reads it: the touch computation only needs the
      // anchors' names and derived transforms.
      boost::intrusive_ptr<Resource<GeometryData> > geometry =
          GetContext().geometry_manager.Fetch(dirpart + aseFilename, true);
      if (properties.GetBool("dynamic")) geometry->GetResource()->SetDynamic(true);

      std::unique_ptr<ImportNode> anchor(
          new ImportNode(objectName, ImportNodeKind::Anchor));
      anchor->SetLocalMode(localMode);
      anchor->SetPosition(position);
      anchor->SetRotation(rotation);
      node->AddChild(std::move(anchor));
    }

    // LIGHT
    //
    // Dropped with the scene graph: the old branch built a Light object, which
    // only ever existed to be rendered, and lights are not part of the import
    // hierarchy. No asset in use contains one.

    objectIter++;
  }

  return node;
}

void ImportLoader::InterpretProperties(const map_XMLTree& tree,
                                      Properties& properties) const {
  map_XMLTree::const_iterator propIter = tree.begin();
  while (propIter != tree.end()) {
    DO_VALIDATION;
    properties.Set(propIter->first.c_str(), propIter->second.value);
    propIter++;
  }
}

e_LocalMode ImportLoader::InterpretLocalMode(const std::string& value) const {
  if (value.compare("absolute") == 0) {
    DO_VALIDATION;
    return e_LocalMode_Absolute;
  } else {
    return e_LocalMode_Relative;
  }
}

}  // namespace blunted
