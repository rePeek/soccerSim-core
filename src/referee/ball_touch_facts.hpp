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
#include "football/player/player_definition.hpp"
#include "referee/referee_facts.hpp"
#include "referee/referee_player_fact.hpp"

namespace football::sim::rules {

// How the ball was played at the moment of this touch. Law 11 treats a
// deliberate play by a defender as resetting the offside position, while a
// deflection or a deliberate save leaves it standing.
enum class BallPlayKind {
  DeliberatePlay,
  Deflection,
  DeliberateSave,
};

// Explicit value facts for one synchronous touch notification. Runtime reads the
// competition facts and samples them at the touch boundary; the referee only
// consumes these values and never reaches into Ball, Player, Team or restart
// execution state. The all-active span is required only while offside
// evaluation is enabled and must outlive the call.
struct BallTouchFacts {
  football::sim::Tick now{};
  football::player::PlayerId touch_player = football::player::kInvalidPlayerId;
  int touch_team_id = -1;
  // The toucher's Stadium Frame position, for the offside restart spot.
  football::math::Vector3 touch_position;
  RefereeTeamFact touch_team;
  RefereeTeamFact defending_team;
  bool in_play = false;
  bool in_set_piece = false;
  bool offsides_enabled = false;
  RefereeBallFact ball;
  const football::pitch::Pitch* pitch = nullptr;
  std::span<const RefereePlayerFact> all_active_players;
  RestartRuleFacts restart;
  // Release verdict inputs, sampled at this exact touch boundary.
  RestartReleaseFacts release;
  BallPlayKind play_kind = BallPlayKind::DeliberatePlay;
};

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_BALL_TOUCH_FACTS_HPP
