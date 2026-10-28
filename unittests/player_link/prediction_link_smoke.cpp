#include <cmath>
#include "football/player/execution/player_motion.hpp"
#include "football/player/execution/player_body_facing.hpp"
#include "football/player/execution/player_pass_solver.hpp"
// Legacy public paths remain source-compatible when linked with Execution.
#include "football/player/command/player_motion.hpp"
#include "football/player/command/player_motion_profile.hpp"
#include "football/player/command/player_body_facing.hpp"
#include "football/player/command/player_pass_solver.hpp"

int main() {
  using football::math::Vector3;
  PlayerKinematicState state;
  PlayerMotionProfile profile;
  const auto step = PlayerMotion::StepContinuous(
      state, profile, Vector3(1, 0, 0), Vector3(0, -1, 0), 0.01f);
  if (!(step.state.position.GetLength() > 0) || state.position.GetLength() != 0) return 1;
  auto body = step.state;
  PlayerBodyFacing::Step(body, PlayerBodyFacingInput{}, PlayerBodyFacingParameters{}, 0.01f);
  if (!std::isfinite(body.bodyFacing.GetLength())) return 2;
  football::sim::player::PassSolverInput pass;
  if (football::sim::player::SolvePass(pass).solved) return 3;
  pass.target_position = Vector3(10, 0, 0);
  const auto result = football::sim::player::SolvePass(pass);
  if (!result.solved || !(result.power > 0) || result.direction.coords[0] <= 0) return 4;
  return 0;
}
