#pragma once

#include "football/ball/ball.hpp"
#include "football/ball/ball_contact.hpp"
#include "football/pitch/pitch.hpp"

class Env {
 public:
  explicit Env(football::pitch::PitchModel pitch_model,
               football::ball::BallModel ball_model);
  ~Env();

  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(Env&&) = delete;

 private:
  football::pitch::Pitch pitch_;
  football::ball::Ball ball_;
};
