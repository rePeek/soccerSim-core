// Ported from the legacy GoalieDefaultStrategy positioning plus the
// PlayerController keeper deflect gate. Ball predictions come from the public
// observation; the RNG and the MentalImage are gone, and hands legality stays
// with Rules.
#include "keeper.hpp"

#include <algorithm>
#include <cmath>

#include "eliza_observation.hpp"
#include "football/foundation/math/scalar.hpp"

namespace football::ai::eliza {
namespace {
using football::math::clamp;
using football::math::Vector3;

// Legacy penalty-area literals (16.4 m depth, 20.05 m half width).
constexpr float kBoxDepth = 16.4f;
constexpr float kBoxHalfWidth = 20.05f;
}  // namespace

KeeperDecision DecideKeeper(const WorldState& world, const WorldPlayerState& self,
                            const ElizaParameters& /*parameters*/) {
  const float defend = static_cast<float>(TeamOf(world, self.side).defending_direction);
  const Vector3 goal(defend * world.pitch.half_length(), 0.0f, 0.0f);
  const Vector3 ball = world.ball_position.Get2D();
  const Vector3 goal_to_ball = ball - goal;
  const float ball_goal_distance = goal_to_ball.GetLength();

  const Vector3 forward = goal_to_ball.GetNormalized(Vector3(-defend, 0.0f, 0.0f));
  Vector3 target = goal + forward * clamp(ball_goal_distance * 0.12f, 0.0f, kBoxDepth);
  const float box_x_a = goal.coords[0];
  const float box_x_b = goal.coords[0] + defend * kBoxDepth;
  target.coords[0] = clamp(target.coords[0], std::min(box_x_a, box_x_b),
                           std::max(box_x_a, box_x_b));
  target.coords[1] = clamp(target.coords[1], -kBoxHalfWidth, kBoxHalfWidth);

  KeeperDecision decision;
  decision.target = target;

  const bool ball_in_box =
      std::fabs(world.ball_position.coords[1]) <= kBoxHalfWidth &&
      world.ball_position.coords[0] * -defend <=
          -world.pitch.half_length() + kBoxDepth;
  const float keeper_to_ball = (self.position - world.ball_position).Get2D().GetLength();
  const bool loose = !world.ball_retainer.has_value();
  decision.rush =
      loose && ball_in_box && ball_goal_distance < 18.0f && keeper_to_ball < 12.0f;
  if (decision.rush) decision.target = ball;
  decision.save = loose && ball_in_box && world.ball_position.coords[2] < 2.0f &&
      keeper_to_ball < 4.0f;
  return decision;
}

}  // namespace football::ai::eliza
