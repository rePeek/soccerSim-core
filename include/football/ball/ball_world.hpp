#ifndef FOOTBALL_BALL_BALL_WORLD_HPP
#define FOOTBALL_BALL_BALL_WORLD_HPP

#include <vector>

#include "football/ball/collider.hpp"

namespace football::ball {

// Ball's complete static physical environment, assembled by the caller (the
// Pitch module) from the pitch description. Ball never reads football::pitch::Pitch and
// never builds pitch geometry itself; it only consumes this value.
struct GroundSurface {
  float deceleration = 1.6f;
  float quadratic_resistance = 0.04f;
  float grass_height = 0.025f;
};

// Goal/net dimensions needed by the discrete net-damping correction. The
// posts and crossbars are ordinary static colliders (static_colliders), not a
// second woodwork solver.
struct GoalNetGeometry {
  float pitch_half_length = 52.5f;
  float goal_half_width = 3.66f;
  float goal_height = 2.44f;
  float goal_depth = 2.55f;
};

struct BallWorld {
  // Ground plane and goal posts/crossbars, with stable ids and order.
  std::vector<ColliderMotion> static_colliders;
  GroundSurface ground;
  GoalNetGeometry net;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_WORLD_HPP
