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


#ifndef FOOTBALL_SIM_PLAYER_REACHABILITY_HPP
#define FOOTBALL_SIM_PLAYER_REACHABILITY_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/animation/types.hpp"
#include "football/player/state/player_kinematics.hpp"
#include "player/command/player_motion_constants.hpp"
#include <optional>

namespace football::ball { class Ball; }
#include "football/foundation/time/tick.hpp"
#include "player/command/player_motion_constants.hpp"
#include <optional>

using football::math::Vector3;

namespace football::sim::player {

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
    const Vector3 &targetPos, float maxVelocity = playerSprintVelocity,
    bool precise = false, std::optional<TickSpan> horizon = std::nullopt);

// P19.4: the player's own reachability and ball control, as one pure value.
// It reads only the actor's kinematic state and the ball's predicted motion;
// the cross-team possession comparison stays in Runtime. This is the value a
// future Player AI can consume as an Observation without calling Player::Process.
struct PlayerReachabilityFacts {
  unsigned int usual_ms = 1000;
  unsigned int optimistic_ms = 1000;
  bool locally_controllable = false;
};

struct PlayerReachabilityInput {
  PlayerKinematicState kinematic;
  float max_velocity = 0.0f;
  e_FunctionType action_type = e_FunctionType_None;
  // The actor is in pure locomotion, so the capability model applies; when
  // `recompute` is false the previous estimate is retained (staggered cadence).
  bool pure_locomotion = false;
  bool recompute = false;
  // The team's designated player gets the precise close-range solve.
  bool precise = false;
  bool touch_anim = false;
  bool touch_pending = false;
  int touch_frame = -1;
  int current_frame = 0;
  unsigned int previous_usual_ms = 1000;
  unsigned int previous_optimistic_ms = 1000;
  const football::ball::Ball* ball = nullptr;
};

PlayerReachabilityFacts ComputePlayerReachability(
    const PlayerReachabilityInput& input);

}  // namespace football::sim::player

#endif  // FOOTBALL_SIM_PLAYER_REACHABILITY_HPP
