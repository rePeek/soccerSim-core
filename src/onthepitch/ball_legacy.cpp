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

#include "ball_legacy.hpp"
#include "core/physics/ball_physics.hpp"

#include <cmath>

#include "match.hpp"


BallLegacy::BallLegacy(const football::domain::BallProfile& profile,
                       BallState& state, Match *match)
    : ball_(new football::domain::Ball(profile, state)), match(match) {
  DO_VALIDATION;
  CalculatePrediction();
}

BallLegacy::~BallLegacy() { DO_VALIDATION; }

void BallLegacy::Mirror() {
  ball_->State().velocity.Mirror();
  for (auto &a : predictions) {
    a.Mirror();
  }
  for (auto &a : ballPosHistory) {
    a.Mirror();
  }
  ball_->State().position.Mirror();
}

void BallLegacy::GetPredictionArray(std::vector<Vector3> &target) {
  DO_VALIDATION;
  target.resize(ballPredictionSize_ms / 10);
  for (int x = 0; x < ballPredictionSize_ms / 10; x++) {
    DO_VALIDATION;
    target[x] = predictions[x];
  }
}


Vector3 BallLegacy::GetRotation() {
  DO_VALIDATION;
  return ball_->State().angularVelocity;
}

void BallLegacy::Touch(const Vector3 &target) {
  DO_VALIDATION;
  valid_predictions = 0;
  if (ball_->State().position.coords[2] < 0.11f) ball_->State().position.coords[2] = 0.11f;

  SetMomentum(target);

  // recalculate prediction
  CalculatePrediction();
  match->UpdateLatestMentalImageBallPredictions();

  match->GetTeam(match->FirstTeam())->UpdatePossessionStats();
  match->GetTeam(match->SecondTeam())->UpdatePossessionStats();
}

void BallLegacy::SetPosition(const Vector3 &target) {
  DO_VALIDATION;
  valid_predictions = 0;
  ball_->State().position.Set(target);
  ball_->State().velocity.Set(0);
  SetRotation(0, 0, 0, 1.0);
  ballPosHistory.clear();
}

void BallLegacy::SetMomentum(const Vector3 &target) {
  DO_VALIDATION;
  ball_->State().velocity.Set(target);
  CalculatePrediction();
}

void BallLegacy::SetRotation(real x, real y, real z, float bias) {
  DO_VALIDATION;  // radians per second for each axis
  // Legacy axis convention: x (forward roll) maps to -X, y/z to +Y/+Z.
  const Vector3 target(-x, y, z);
  ball_->State().angularVelocity =
      ball_->State().angularVelocity * (1.0f - bias) + target * bias;
  CalculatePrediction();
}

BallSpatialInfo BallLegacy::CalculatePrediction() {
  DO_VALIDATION;

  Vector3 newVelocity;
  Vector3 newAngularVelocity;


  // fill predictions

  Vector3 nextPos = ball_->State().position;
  Quaternion nextOrientation = ball_->State().orientation;
  Vector3 velocityPredict = ball_->State().velocity;
  Vector3 angularVelocityPredict = ball_->State().angularVelocity;

  predictions[0] = nextPos;


  constexpr float timeStep = 0.01f;//0.001f; // seconds

  bool firstTime = true;
  bool use_cache = false;


  for (unsigned int predictTime_ms = int(timeStep * 1000.0f);
       predictTime_ms < ballPredictionSize_ms + cachedPredictions * 10;
       predictTime_ms += int(timeStep * 1000.0f)) {
    DO_VALIDATION;
    // Originally game was recomputing ball's prediction for 300 steps into the
    // future, which was expensive. Now we cache 100 additional steps and if
    // the ball was not touched etc. we just shift predictions by one.
    if (use_cache) {
      DO_VALIDATION;
      predictions[predictTime_ms / 10] = predictions[predictTime_ms / 10 + 1];
      continue;
    }

    BallState stepState;
    stepState.position = nextPos;
    stepState.velocity = velocityPredict;
    stepState.angularVelocity = angularVelocityPredict;
    stepState.orientation = nextOrientation;
    stepState = BallPhysics::Step(stepState, timeStep, ball_->Profile(),
                                  BallPhysicsParams(), firstTime, GoalGeometry());
    nextPos = stepState.position;
    velocityPredict = stepState.velocity;
    angularVelocityPredict = stepState.angularVelocity;
    nextOrientation = stepState.orientation;

    if (predictTime_ms == 10) {
      DO_VALIDATION;
      newVelocity = velocityPredict;
      newAngularVelocity = angularVelocityPredict;
      orientPrediction = nextOrientation;
      if (valid_predictions > 0 && predictions[2] == nextPos) {
        DO_VALIDATION;
        valid_predictions--;
        use_cache = true;
      } else {
        valid_predictions = cachedPredictions;
      }
    }
    predictions[predictTime_ms / 10] = nextPos;

    firstTime = false;
  }

  return BallSpatialInfo(newVelocity, newAngularVelocity);
}

Vector3 BallLegacy::GetAveragePosition(unsigned int duration_ms) const {
  std::list<Vector3>::const_reverse_iterator iter = ballPosHistory.rbegin();
  unsigned int total = 0;
  Vector3 averageVec;
  while (iter != ballPosHistory.rend()) {
    DO_VALIDATION;
    averageVec += *iter;
    total++;
    if (total * 10 > duration_ms) break;
    iter++;
  }
  if (total > 0) averageVec /= total; else averageVec = Predict(0);
  return averageVec;
}

void BallLegacy::Process() {
  DO_VALIDATION;
  BallSpatialInfo spatialInfo = CalculatePrediction();
  ball_->State().velocity = spatialInfo.velocity;
  ball_->State().angularVelocity = spatialInfo.angularVelocity;

  ball_->State().position = Predict(10);
  ball_->State().orientation = orientPrediction;

  ballPosHistory.push_back(ball_->State().position);
  if (ballPosHistory.size() > ballHistorySize) ballPosHistory.pop_front();
}


void BallLegacy::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  ball_->State().velocity = Vector3(0);
  ball_->State().angularVelocity = Vector3(0);
  for (unsigned int i = 0; i < ballPredictionSize_ms / 10; i++) {
    DO_VALIDATION;
    predictions[i] = Vector3(focusPos + Vector3(0, 0, 0.11));
  }
  orientPrediction = QUATERNION_IDENTITY;
  ballPosHistory.clear();
  ball_->State().position = Vector3(focusPos + Vector3(0, 0, 0.11));
  valid_predictions = 0;
  ball_->State().orientation = QUATERNION_IDENTITY;
}

void BallLegacy::ProcessState(EnvState *state) {
  DO_VALIDATION;
  state->process(ball_->State().velocity);
  state->process(ball_->State().angularVelocity);
  for (int x = 0; x < sizeof(predictions) / sizeof(predictions[0]); x++) {
    state->process(predictions[x]);
  }
  state->process(valid_predictions);
  state->process(orientPrediction);
  int size = ballPosHistory.size();
  state->process(size);
  ballPosHistory.resize(size);
  for (auto &i : ballPosHistory) {
    DO_VALIDATION;
    state->process(i);
  }
  state->process(ball_->State().position);
  state->process(ball_->State().orientation);
}
