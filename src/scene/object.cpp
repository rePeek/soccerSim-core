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

#include "object.hpp"


namespace blunted {

Object::Object(std::string name, e_ObjectType objectType)
    : Spatial(name), objectType(objectType) {
  DO_VALIDATION;
  enabled = true;
}

Object::~Object() { DO_VALIDATION; }

inline e_ObjectType Object::GetObjectType() {
  DO_VALIDATION;
  return objectType;
}

  const Properties &Object::GetProperties() const {
    return properties;
  }

  bool Object::PropertyExists(const char *property) const {
    bool exists = properties.Exists(property);
    return exists;
  }

  const std::string &Object::GetProperty(const char *property) const {
    return properties.Get(property);
  }

  void Object::SetProperties(Properties newProperties) {
    DO_VALIDATION;
    properties.AddProperties(&newProperties);
  }

  void Object::SetProperty(const char *name, const char *value) {
    DO_VALIDATION;
    properties.Set(name, value);
  }

  bool Object::RequestPropertyExists(const char *property) const {
    bool exists = requestProperties.Exists(property);
    return exists;
  }

  std::string Object::GetRequestProperty(const char *property) const {
    std::string result = requestProperties.Get(property);
    return result;
  }

  void Object::AddRequestProperty(const char *property) {
    DO_VALIDATION;
    requestProperties.Set(property, "");
  }

  void Object::SetRequestProperty(const char *property, const char *value) {
    DO_VALIDATION;
    requestProperties.Set(property, value);
  }
}
