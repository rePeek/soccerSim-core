#include "football/env.hpp"

#include "football/log/log.hpp"

using namespace football::log;

Env::Env(PitchModel pitch_model, BallModel ball_model)
    : pitch_(pitch_model), ball_(ball_model) {
  Info(Channel::env, "environment constructed");
}

Env::~Env() {
  Info(Channel::env, "environment destroyed");
  Flush();
}
