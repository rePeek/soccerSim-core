#include "football/env.hpp"

Env::Env(football::pitch::PitchModel pitch_model,
         football::ball::BallModel ball_model)
    : pitch_(pitch_model), ball_(ball_model) {}

Env::~Env() = default;
