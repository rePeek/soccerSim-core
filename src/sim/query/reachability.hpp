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
#include "sim/tick.hpp"
#include <optional>

namespace football::sim::query {

struct TimeNeeded {
  TimeNeeded() {
    usual_ms = 0;
    optimistic_ms = 0;
  }
  unsigned int usual_ms = 0;
  unsigned int optimistic_ms = 0;
};

// The search horizon is discrete; the estimates (including close-range ranking)
// retain millisecond precision and are not grid deadlines. No horizon is unbounded.
TimeNeeded GetTimeNeededForDistance_ms(
    const Vector3 &playerPos, const Vector3 &playerMovement,
    const Vector3 &targetPos, float maxVelocity = sprintVelocity,
    bool precise = false, std::optional<TickSpan> horizon = std::nullopt);

}  // namespace football::sim::query

#endif  // FOOTBALL_SIM_QUERY_REACHABILITY_HPP
