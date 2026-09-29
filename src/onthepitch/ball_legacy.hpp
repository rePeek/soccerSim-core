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

#ifndef _HPP_FOOTBALL_ONTHEPITCH_BALL_LEGACY
#define _HPP_FOOTBALL_ONTHEPITCH_BALL_LEGACY

#include "../base/math/quaternion.hpp"
#include "../base/math/vector3.hpp"
#include "../defines.hpp"
#include "../gamedefines.hpp"
#include "../utils.hpp"
#include "core/model/ball/ball.hpp"
#include "core/model/ball/ball.hpp"
#include "core/physics/ball_physics.hpp"
#include <array>
#include <memory>

using namespace blunted;

class Match;

struct BallSpatialInfo {
  BallSpatialInfo(const Vector3 &velocity, const Vector3 &angularVelocity) { DO_VALIDATION;
    this->velocity = velocity;
    this->angularVelocity = angularVelocity;
  }
  Vector3 velocity;
  Vector3 angularVelocity;
};

// Legacy facade: prediction cache, mental-image / possession updates and the
// Match pointer stay here. The composed football::model::Ball references the state.
class BallLegacy {

  public:
    BallLegacy(const football::model::BallProfile& profile, BallState& state,
               Match *match);
    virtual ~BallLegacy();

    void Mirror();

    inline Vector3 Predict(int predictTime_ms) const {
      int index = predictTime_ms;
      if (index >= ballPredictionSize_ms) index = ballPredictionSize_ms - 10;
      index = index / 10;
      if (index < 0) index = 0;
      return predictions[index];
    }

    // Composed domain entity (Phase 7E.5).
    football::model::Ball& Entity() { return *ball_; }
    const football::model::Ball& Entity() const { return *ball_; }

    void GetPredictionArray(std::vector<Vector3> &target);
    Vector3 GetMovement() { return ball_->State().velocity; }
    Vector3 GetRotation();
    void Touch(const Vector3 &target);
    void SetPosition(const Vector3 &target);
    void SetMomentum(const Vector3 &target);
    void SetRotation(real x, real y, real z, float bias = 1.0);     // radians per second for each axis
    BallSpatialInfo CalculatePrediction();
    BallSpatialInfo CalculatePrediction(
        const std::vector<PlayerBodyCandidate> &players);

    Vector3 GetAveragePosition(unsigned int duration_ms) const;

    void Process();
    void Process(const std::vector<PlayerBodyCandidate> &players);
    const std::array<BallImpact, 8> &GetLastStepImpacts() const {
      return lastStepImpacts;
    }
    uint8_t GetLastStepImpactCount() const { return lastStepImpactCount; }
    Quaternion GetOrientation() const { return ball_->State().orientation; }

    void ResetSituation(const Vector3 &focusPos);
    void ProcessState(EnvState *state);
    std::unique_ptr<football::model::Ball> ball_;  // composed domain entity

    Vector3 predictions[ballPredictionSize_ms / 10 + cachedPredictions + 1];
    int valid_predictions = 0;
    Quaternion orientPrediction;

    std::list<Vector3> ballPosHistory;

    Match *match;

    // Impacts from the authoritative 10ms tick (prediction steps ignore
    // impacts; they only produce a trajectory).
    std::array<BallImpact, 8> lastStepImpacts;
    uint8_t lastStepImpactCount = 0;


};

#endif
