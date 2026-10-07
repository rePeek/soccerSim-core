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

#ifndef _HPP_FOOTBALL_ONTHEPITCH_BALL
#define _HPP_FOOTBALL_ONTHEPITCH_BALL

#include <algorithm>
#include <vector>

#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"
#include "sim/ball/ball_timing.hpp"
#include "sim/ball/ball_environment.hpp"
#include "model/pitch.hpp"

using namespace blunted;

struct BallSpatialInfo {
  BallSpatialInfo(const Vector3 &momentum, const Quaternion &rotation_ms) {
    this->momentum = momentum;
    this->rotation_ms = rotation_ms;
  }
  Vector3 momentum;
  Quaternion rotation_ms;
};

class Ball {

  public:
    explicit Ball(const football::model::Pitch& pitch);
    virtual ~Ball();

    void Mirror();

    Vector3 Predict(football::sim::TickSpan horizon) const {
      const auto last = football::sim::ball_timing::kPredictionHorizon - football::sim::TickSpan{1};
      return predictions[std::min(horizon, last).value];
    }
    // Temporary calculation adapter: retain the old explicit sampling policy
    // (negative -> zero; positive sub-tick -> previous sample). Not a deadline.
    Vector3 Predict(int predictTime_ms) const {
      return Predict(football::sim::TickSpan{static_cast<std::uint64_t>(
          std::max(predictTime_ms, 0)) / football::sim::kMillisecondsPerTick});
    }

    void GetPredictionArray(std::vector<Vector3> &target) const;
    Vector3 GetMovement() const;
    Vector3 GetRotation() const;
    // Physics only: callers own touch accounting, observation and possession refresh.
    void Touch(const Vector3 &target, football::sim::BallEnvironment environment);
    void SetPosition(const Vector3 &target, football::sim::BallEnvironment environment);
    void SetMomentum(const Vector3 &target, football::sim::BallEnvironment environment);
    // Radians per second for each axis.
    void SetRotation(real x, real y, real z, float bias,
                     football::sim::BallEnvironment environment);
    BallSpatialInfo CalculatePrediction(football::sim::BallEnvironment environment);

    void Process(football::sim::BallEnvironment environment);
    Quaternion GetOrientation() const { return orientationBuffer; }

    void ResetSituation(const Vector3 &focusPos);
  private:
    Vector3 momentum;
    Quaternion rotation_ms;

    Vector3 predictions[football::sim::ball_timing::kPredictionHorizon.value +
                        football::sim::ball_timing::kPredictionCache.value + 1];
    int valid_predictions = 0;
    Quaternion orientPrediction;


    Vector3 positionBuffer;
    Quaternion orientationBuffer;

    const football::model::Pitch pitch_;


};

#endif
