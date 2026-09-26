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

#include "geometry.hpp"

#include "../../base/log.hpp"
#include "../../main.hpp"

#include "../../main.hpp"

namespace blunted {

Geometry::Geometry(std::string name, e_ObjectType objectType)
    : Object(name, objectType) {
  DO_VALIDATION;
}

Geometry::~Geometry() { DO_VALIDATION; }

void Geometry::SetGeometryData(
    boost::intrusive_ptr<Resource<GeometryData> > geometryData) {
  DO_VALIDATION;
  this->geometryData = geometryData;
  InvalidateBoundingVolume();
}

boost::intrusive_ptr<Resource<GeometryData> > Geometry::GetGeometryData() {
  DO_VALIDATION;
  return geometryData;
}

void Geometry::RecursiveUpdateSpatialData(e_SpatialDataType spatialDataType,
                                          e_SystemType excludeSystem) {
  DO_VALIDATION;
  InvalidateSpatialData();
  InvalidateBoundingVolume();
}

  AABB Geometry::GetAABB() const {
    if (aabb.dirty == true) {
      DO_VALIDATION;
      assert(geometryData->GetResource());
      aabb.aabb = geometryData->GetResource()->GetAABB() * GetDerivedRotation() + GetDerivedPosition();
      aabb.dirty = false;
    }

    AABB tmp = aabb.aabb;
    return tmp;
  }

}
