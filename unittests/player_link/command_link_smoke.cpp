#include "football/player/command/player_motion.hpp"

int main() {
  PlayerKinematicState state;
  PlayerMotionProfile profile;
  const MotionStep step = PlayerMotion::StepContinuous(
      state, profile, football::math::Vector3(1, 0, 0),
      football::math::Vector3(0, -1, 0), 0.01f);
  return step.state.position.GetLength() >= 0.0f ? 0 : 1;
}
