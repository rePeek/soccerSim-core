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


#ifndef FOOTBALL_SIM_RULES_BALL_TOUCH_FACTS_HPP
#define FOOTBALL_SIM_RULES_BALL_TOUCH_FACTS_HPP

#include <span>

#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"
#include "observation/pitch_frame.hpp"
#include "sim/runtime/team_view.hpp"

namespace football::ball { class Ball; }
class Player;

namespace football::sim::rules {

// Explicit facts for one synchronous touch notification. Competition facts are
// read by the caller; the referee only consumes these values. The all-active
// span is required only while offside evaluation is enabled and must outlive
// the call.
struct BallTouchFacts {
  football::sim::Tick now{};
  Player* touch_player = nullptr;
  int touch_team_id = -1;
  football::sim::TeamView touch_team;
  football::sim::TeamView defending_team;
  bool in_play = false;
  bool in_set_piece = false;
  bool offsides_enabled = false;
  const football::ball::Ball* ball = nullptr;
  const football::pitch::Pitch* pitch = nullptr;
  std::span<Player* const> all_active_players;
  PitchFrameTransform stadium_to_home{false};
  bool preserve_opponent_offside = false;
  bool physical_kick = false;
};

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_BALL_TOUCH_FACTS_HPP
