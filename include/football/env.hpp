#pragma once

#include "football/ball/ball.hpp"
#include "football/ball/ball_contact.hpp"
#include "football/pitch/pitch.hpp"

using namespace football::ball;
using namespace football::pitch;

class Env {
 public:
  explicit Env(PitchModel pitch_model, BallModel ball_model);
  ~Env();

  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(Env&&) = delete;

 private:
  Pitch pitch_;
  Ball ball_;
};
