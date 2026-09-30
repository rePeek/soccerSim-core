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

#ifndef _HPP_FOOTBALL_IMPORT_LOADER
#define _HPP_FOOTBALL_IMPORT_LOADER

// Reads an object XML file into the CPU-only import hierarchy.
//
// This replaces utils/objectloader, which built a scene graph (Node tree with
// Geometry/Light objects, renderer attachment, bounding volumes) for the same
// job. What the file format actually carries here is names, transforms and
// local modes, so that is what is produced now.

#include "foundation/properties.hpp"
#include "foundation/xml_loader.hpp"
#include "animation/import_hierarchy.hpp"

namespace blunted {

class ImportLoader {
 public:
  ImportLoader() {}
  ~ImportLoader() {}

  ImportHierarchy LoadObject(const std::string& filename,
                             const Vector3& offset = Vector3(0)) const;

 private:
  std::unique_ptr<ImportNode> LoadNode(const std::string& nodename,
                                       const XMLTree& objectTree,
                                       const Vector3& offset) const;

  e_LocalMode InterpretLocalMode(const std::string& value) const;
};

}  // namespace blunted

#endif
