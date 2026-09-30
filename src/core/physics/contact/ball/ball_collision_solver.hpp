#ifndef FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_COLLISION_SOLVER_HPP
#define FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_COLLISION_SOLVER_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "core/model/ball/ball.hpp"
#include "core/physics/contact/ball/ball_impact.hpp"
#include "core/physics/contact/ball/goal_geometry.hpp"
#include "core/physics/contact/ball/player_body_candidate.hpp"

namespace football_sim::contact {

struct BallContactStepResult {
  football_sim::BallState state;
  std::array<BallImpact, 8> impacts{};
  uint8_t impactCount = 0;
};

// Advances the ball through one tick using swept/CCD contact against ground,
// woodwork and moving player bodies, then applies persistent ground contact.
// Pure: consumes a state and returns the next state and impact attribution.
[[nodiscard]] BallContactStepResult SolveBallContacts(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    float dt, bool applyWoodwork, const GoalGeometry& goal,
    const std::vector<PlayerBodyCandidate>& players);

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_COLLISION_SOLVER_HPP