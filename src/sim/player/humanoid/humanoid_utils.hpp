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

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_HUMANOID_UTILS
#define _HPP_HUMANOID_UTILS

#include "sim/ball/ball_touch.hpp"

#include "sim/ball/ball.hpp"

#include "foundation/math/vector3.hpp"
#include "sim/random/rng.hpp"

using namespace blunted;

struct SpatialState;
class Player;
class Team;
struct AnimationClip;
namespace football::sim::event { struct TouchState; }
struct Anim;

e_TouchType GetTouchTypeForBodyPart(const std::string &bodypartname);
float CalculateBiasForFastCornering(const Vector3 &currentMovement,
                                    const Vector3 &desiredMovement,
                                    float veloPow = 1.0f, float bias = 1.0f);
Vector3 CalculateMovementAtFrame(const std::vector<Vector3> &positions, unsigned int frameNum, unsigned int smoothFrames = 0);
Vector3 GetFrontOfFootOffsetRel(float velocity, radian bodyAngleRel, float height);
bool NeedDefendingMovement(int mySide, const Vector3 &position, const Vector3 &target);
float StretchSprintTo(const float &inputVelocity, float inputSpaceMaxVelocity, float targetMaxVelocity);
void GetDifficultyFactors(Ball *ball, Player *player, const football::sim::event::TouchState& touches, Team& opponent, football::sim::Tick now, SimulationRng& rng, const SpatialState &spatialState, const Vector3 &positionOffset, float &distanceFactor, float &heightFactor, float &ballMovementFactor);
Vector3 GetBallControlVector(Ball *ball, Player *player, Team& opponent, const AnimationClip& clip, const Vector3 &nextStartPos, radian nextStartAngle, radian nextBodyAngle, const Vector3 &outgoingMovement, const Anim &currentAnim, int frameNum, const SpatialState &spatialState, const Vector3 &positionOffset, radian &xRot, radian &yRot, float ffoOffset = 0.0f);
Vector3 GetTrapVector(Ball *ball, Player *player, const football::sim::event::TouchState& touches, Team& opponent, football::sim::Tick now, SimulationRng& rng, const AnimationClip& clip, const Vector3 &nextStartPos, radian nextStartAngle, radian nextBodyAngle, const Vector3 &outgoingMovement, const Anim &currentAnim, int frameNum, const SpatialState &spatialState, const Vector3 &positionOffset, radian &xRot, radian &yRot);
Vector3 GetShotVector(Ball *ball, Player *player, const AnimationClip& clip, SimulationRng& rng, const Anim &currentAnim, const SpatialState &spatialState, const Vector3 &positionOffset, radian &xRot, radian &yRot, radian &zRot);

#endif
