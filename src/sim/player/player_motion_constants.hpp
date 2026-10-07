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

#ifndef FOOTBALL_SIM_PLAYER_PLAYER_MOTION_CONSTANTS_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_MOTION_CONSTANTS_HPP

const float animSprintVelocity = 7.0f;

// PES6 digital control mode, quantizes some input to x degree angles
const bool quantizeDirection = true;
const float analogStickDeadzone = 0.75f;
const float _default_QuantizedDirectionBias = 0.0f;
const float _default_AgilityFactor = 0.5f;
const float _default_AccelerationFactor = 0.5f;
const float _default_ShortPass_AutoDirection = 0.4f;
const float _default_ShortPass_AutoPower = 0.7f;
const float _default_ThroughPass_AutoDirection = 0.2f;
const float _default_ThroughPass_AutoPower = 0.7f;
const float _default_HighPass_AutoDirection = 0.2f;
const float _default_HighPass_AutoPower = 0.5f;
const float _default_Shot_AutoDirection = 0.2f;

const float distanceToVelocityMultiplier = 2.6f; // for example: when we need to travel 4 meters, we need to go at velo 4 * distanceToVelocityMultiplier

// how far into an animation the ball is usually touched
const unsigned int defaultTouchOffset_ms = 80;
const float defaultPlayerHeight = 1.92f;
const int temporalSmoother_history_ms = 20;

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_MOTION_CONSTANTS_HPP
