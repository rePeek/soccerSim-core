#pragma once


#include "football/ball/ball.hpp"
#include "football/ball/ball_contact.hpp"
#include "football/pitch/pitch.hpp"

// Minimal environment facade: one authoritative Ball on one Pitch. There is no
// match clock, team, player, referee or AI layer in this build.
class GameEnv {
 public:
  explicit GameEnv(football::pitch::Pitch pitch,
                   football::ball::BallModel ball_model = {});
  ~GameEnv();

  GameEnv(const GameEnv&) = delete;
  GameEnv& operator=(const GameEnv&) = delete;
  GameEnv(GameEnv&&) = delete;
  GameEnv& operator=(GameEnv&&) = delete;

  // Reset the authoritative ball state without changing the pitch or model.
  void Reset(const football::ball::BallState& state);

  // One native 10 ms physics tick against the pitch's static scene.
  football::ball::BallStepResult Step();

  const football::ball::BallState& Observe() const { return ball_.get_state(); }
  football::ball::Ball& ball() { return ball_; }
  const football::ball::Ball& ball() const { return ball_; }
  const football::pitch::Pitch& pitch() const { return pitch_; }
  const football::ball::BallModel& ball_model() const { return ball_.model(); }

 private:
  football::pitch::Pitch pitch_;
  football::ball::Ball ball_;
};
