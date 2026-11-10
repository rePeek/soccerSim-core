#include "football/env.hpp"

#include <utility>

GameEnv::GameEnv(football::pitch::Pitch pitch,
                 football::ball::BallModel ball_model)
    : pitch_(std::move(pitch)), ball_(ball_model, pitch_) {}

GameEnv::~GameEnv() = default;

void GameEnv::Reset(const football::ball::BallState& state) { ball_.set_state(state); }

football::ball::BallStepResult GameEnv::Step() { return ball_.Step(); }
