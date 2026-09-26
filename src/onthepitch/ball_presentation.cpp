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

#include "ball_presentation.hpp"

#include "../utils/objectloader.hpp"

BallPresentation::BallPresentation(
    boost::intrusive_ptr<blunted::Node> dynamicNode)
    : dynamicNode(dynamicNode) {
  DO_VALIDATION;
  blunted::ObjectLoader loader;
  ballNode = loader.LoadObject("media/objects/balls/generic.object");
  dynamicNode->AddNode(ballNode);

  std::list<boost::intrusive_ptr<blunted::Geometry> > children;
  ballNode->GetObjects<blunted::Geometry>(blunted::e_ObjectType_Geometry,
                                          children);
  ballGeometry = *children.begin();
}

BallPresentation::~BallPresentation() {
  DO_VALIDATION;
  dynamicNode->DeleteNode(ballNode);
}

void BallPresentation::Put(const blunted::Vector3 &position,
                           const blunted::Quaternion &orientation) {
  DO_VALIDATION;
  ballGeometry->SetPosition(position, false);
  ballGeometry->SetRotation(orientation, false);
}
