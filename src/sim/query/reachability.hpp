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


#ifndef FOOTBALL_SIM_QUERY_REACHABILITY_HPP
#define FOOTBALL_SIM_QUERY_REACHABILITY_HPP

#include "sim/gamedefines.hpp"

namespace football::sim::query {

struct TimeNeeded {
  TimeNeeded() {
    usual_ms = 0;
    optimistic_ms = 0;
  }
  unsigned int usual_ms = 0;
  unsigned int optimistic_ms = 0;
};

// Kinematic reachability estimate, including the legacy 10 ms horizon search.
TimeNeeded GetTimeNeededForDistance_ms(
    const Vector3 &playerPos, const Vector3 &playerMovement,
    const Vector3 &targetPos, float maxVelocity = sprintVelocity,
    bool precise = false, unsigned int maxTime_ms = -1);

}  // namespace football::sim::query

#endif  // FOOTBALL_SIM_QUERY_REACHABILITY_HPP
