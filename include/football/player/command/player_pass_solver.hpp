#ifndef FOOTBALL_PLAYER_PASS_SOLVER_HPP
#define FOOTBALL_PLAYER_PASS_SOLVER_HPP

#include <optional>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::sim::player {

// Pure pass solve input. The caller resolves a teammate to values; the solver
// never discovers a receiver and never touches an actor.
struct PassSolverOrigin {
  football::math::Vector3 position;
  football::math::Vector3 movement;
  bool has_touch_position = false;
  football::math::Vector3 touch_position;
  int defending_direction = 0;
};

struct PassSolverTarget {
  football::math::Vector3 position;
  football::math::Vector3 movement;
};

struct PassSolverInput {
  PassSolverOrigin origin;
  e_FunctionType pass_type = e_FunctionType_ShortPass;
  std::optional<PassSolverTarget> target_player;
  std::optional<football::math::Vector3> target_position;
};

struct PassSolverResult {
  football::math::Vector3 direction;
  float power = 0.0f;
  bool solved = false;
  // True when the target came from target_player rather than target_position.
  bool targets_player = false;
};

PassSolverResult SolvePass(const PassSolverInput& input);

}  // namespace football::sim::player

#endif  // FOOTBALL_PLAYER_PASS_SOLVER_HPP
