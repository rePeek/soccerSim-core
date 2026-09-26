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

#include "light.hpp"



namespace blunted {

Light::Light(std::string name) : Object(name, e_ObjectType_Light) {
  DO_VALIDATION;
  radius = 512;
  color.Set(1, 1, 1);
  lightType = e_LightType_Point;
  shadow = false;
}

Light::~Light() { DO_VALIDATION; }

  void Light::SetColor(const Vector3 &color) {
    DO_VALIDATION;
    this->color = color;
  }

  Vector3 Light::GetColor() const {
    Vector3 retColor = color;
    return retColor;

  }

  void Light::SetRadius(float radius) {
    DO_VALIDATION;
    this->radius = radius;
    InvalidateBoundingVolume();
  }

  float Light::GetRadius() const {
    float rad = radius;
    return rad;
  }

  e_LightType Light::GetType() const {
    e_LightType theType = lightType;
    return theType;
  }

  bool Light::GetShadow() const {
    return shadow;
  }

  void Light::SetType(e_LightType lightType) {
    DO_VALIDATION;
    this->lightType = lightType;
  }

  void Light::SetShadow(bool shadow) {
    DO_VALIDATION;
    this->shadow = shadow;
  }

  void Light::RecursiveUpdateSpatialData(e_SpatialDataType spatialDataType,
                                         e_SystemType excludeSystem) {
    DO_VALIDATION;
    InvalidateSpatialData();
    InvalidateBoundingVolume();
  }

  AABB Light::GetAABB() const {
    //aabb.Lock();
    if (aabb.dirty == true) {
      DO_VALIDATION;
      Vector3 pos = GetDerivedPosition();
      aabb.aabb.minxyz = pos - radius;
      aabb.aabb.maxxyz = pos + radius;
      aabb.aabb.MakeDirty();
      aabb.dirty = false;
    }
    AABB tmp = aabb.aabb;
    //aabb.Unlock();
    return tmp;
  }

}
