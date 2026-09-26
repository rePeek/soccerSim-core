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

#ifndef _HPP_FOOTBALL_ONTHEPITCH_BALL_PRESENTATION
#define _HPP_FOOTBALL_ONTHEPITCH_BALL_PRESENTATION

#include "../scene/objects/geometry.hpp"
#include "../scene/scene3d/node.hpp"

class BallPresentation {
 public:
  explicit BallPresentation(boost::intrusive_ptr<blunted::Node> dynamicNode);
  ~BallPresentation();

  void Put(const blunted::Vector3 &position,
           const blunted::Quaternion &orientation);

 private:
  boost::intrusive_ptr<blunted::Node> dynamicNode;
  boost::intrusive_ptr<blunted::Node> ballNode;
  boost::intrusive_ptr<blunted::Geometry> ballGeometry;
};

#endif
