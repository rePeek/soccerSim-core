#ifndef FOOTBALL_CORE_PHYSICS_BALL_SOLVER_HPP
#define FOOTBALL_CORE_PHYSICS_BALL_SOLVER_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "core/model/ball/ball.hpp"
#include "core/physics/contact/ball/ball_impact.hpp"
#include "core/physics/contact/ball/goal_geometry.hpp"
#include "core/physics/contact/ball/player_body_candidate.hpp"

namespace football_sim::physics {

struct BallPhysicsStepResult {
  std::array<football_sim::contact::BallImpact, 8> impacts{};
  uint8_t impactCount = 0;
};

// One authoritative ball tick: free-flight forces, contact solving and
// orientation, committed once through Ball::SetState.
//
// Prediction should call the movement primitives directly; the authoritative
// tick is the only place that receives moving player-body candidates.
[[nodiscard]] BallPhysicsStepResult SolveBallTick(
    football_sim::Ball& ball, float dt, bool applyWoodwork,
    const football_sim::contact::GoalGeometry& goal,
    const std::vector<football_sim::contact::PlayerBodyCandidate>& players);

}  // namespace football_sim::physics

#endif  // FOOTBALL_CORE_PHYSICS_BALL_SOLVER_HPP