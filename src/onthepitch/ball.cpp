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

#include "ball.hpp"
#include "core/physics/ball_physics.hpp"

#include <cmath>

#include "match.hpp"


Ball::Ball(Match *match, BallState& state) : match(match), state_(state) {
  DO_VALIDATION;
  CalculatePrediction();
}

Ball::~Ball() { DO_VALIDATION; }

void Ball::Mirror() {
  state_.momentum.Mirror();
  for (auto &a : predictions) {
    a.Mirror();
  }
  for (auto &a : ballPosHistory) {
    a.Mirror();
  }
  state_.position.Mirror();
}

void Ball::GetPredictionArray(std::vector<Vector3> &target) {
  DO_VALIDATION;
  target.resize(ballPredictionSize_ms / 10);
  for (int x = 0; x < ballPredictionSize_ms / 10; x++) {
    DO_VALIDATION;
    target[x] = predictions[x];
  }
}

Vector3 Ball::GetMovement() {
  DO_VALIDATION;
  // meters / sec
  return state_.momentum;
}

Vector3 Ball::GetRotation() {
  DO_VALIDATION;
  real x, y, z;
  state_.rotation_ms.GetAngles(x, y, z);
  return Vector3(x, y, z);
}

void Ball::Touch(const Vector3 &target) {
  DO_VALIDATION;
  valid_predictions = 0;
  if (state_.position.coords[2] < 0.11f) state_.position.coords[2] = 0.11f;

  SetMomentum(target);

  // recalculate prediction
  CalculatePrediction();
  match->UpdateLatestMentalImageBallPredictions();

  match->GetTeam(match->FirstTeam())->UpdatePossessionStats();
  match->GetTeam(match->SecondTeam())->UpdatePossessionStats();
}

void Ball::SetPosition(const Vector3 &target) {
  DO_VALIDATION;
  valid_predictions = 0;
  state_.position.Set(target);
  state_.momentum.Set(0);
  SetRotation(0, 0, 0, 1.0);
  ballPosHistory.clear();
}

void Ball::SetMomentum(const Vector3 &target) {
  DO_VALIDATION;
  state_.momentum.Set(target);
  CalculatePrediction();
}

void Ball::SetRotation(real x, real y, real z, float bias) {
  DO_VALIDATION;  // radians per second for each axis
  Quaternion rotX;
  rotX.SetAngleAxis(clamp(x * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(-1, 0, 0));
  Quaternion rotY;
  rotY.SetAngleAxis(clamp(y * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(0, 1, 0));
  Quaternion rotZ;
  rotZ.SetAngleAxis(clamp(z * 0.001f, -pi * 0.49f, pi * 0.49f), Vector3(0, 0, 1));

  Quaternion tmpRotation_ms = rotX * rotY * rotZ;
  state_.rotation_ms = state_.rotation_ms.GetSlerped(bias, tmpRotation_ms);

  CalculatePrediction();
}

BallSpatialInfo Ball::CalculatePrediction() {
  DO_VALIDATION;

  Vector3 newMomentum;
  Quaternion newRotation_ms;


  // fill predictions

  Vector3 nextPos = state_.position;
  Quaternion nextOrientation = state_.orientation;
  Vector3 momentumPredict = state_.momentum;
  Quaternion rotationPredict_ms = state_.rotation_ms;

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
    stepState.momentum = momentumPredict;
    stepState.rotation_ms = rotationPredict_ms;
    stepState.orientation = nextOrientation;
    stepState = BallPhysics::Step(stepState, timeStep, BallPhysicsParams(),
                                  firstTime, GoalGeometry());
    nextPos = stepState.position;
    momentumPredict = stepState.momentum;
    rotationPredict_ms = stepState.rotation_ms;
    nextOrientation = stepState.orientation;

    if (predictTime_ms == 10) {
      DO_VALIDATION;
      newMomentum = momentumPredict;
      newRotation_ms = rotationPredict_ms;
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

  return BallSpatialInfo(newMomentum, newRotation_ms);
}

Vector3 Ball::GetAveragePosition(unsigned int duration_ms) const {
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

void Ball::Process() {
  DO_VALIDATION;
  BallSpatialInfo spatialInfo = CalculatePrediction();
  state_.momentum = spatialInfo.momentum;
  state_.rotation_ms = spatialInfo.rotation_ms;

  state_.position = Predict(10);
  state_.orientation = orientPrediction;

  ballPosHistory.push_back(state_.position);
  if (ballPosHistory.size() > ballHistorySize) ballPosHistory.pop_front();
}


void Ball::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  state_.momentum = Vector3(0);
  state_.rotation_ms = QUATERNION_IDENTITY;
  for (unsigned int i = 0; i < ballPredictionSize_ms / 10; i++) {
    DO_VALIDATION;
    predictions[i] = Vector3(focusPos + Vector3(0, 0, 0.11));
  }
  orientPrediction = QUATERNION_IDENTITY;
  ballPosHistory.clear();
  state_.position = Vector3(focusPos + Vector3(0, 0, 0.11));
  valid_predictions = 0;
  state_.orientation = QUATERNION_IDENTITY;
}

void Ball::ProcessState(EnvState *state) {
  DO_VALIDATION;
  state->process(state_.momentum);
  state->process(state_.rotation_ms);
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
  state->process(state_.position);
  state->process(state_.orientation);
}
