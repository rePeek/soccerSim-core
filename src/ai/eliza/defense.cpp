// Ported from the legacy PlayerController defensive helpers
// (GetDefendPosition, AddDefensiveComponent). Player*/MentalImage* queries
// became public Observation values; the geometry is unchanged.
#include "defense.hpp"

#include <cmath>

#include "eliza_geometry.hpp"
#include "eliza_observation.hpp"
#include "football/foundation/math/scalar.hpp"

namespace football::ai::eliza {
namespace {
using football::math::clamp;
using football::math::Vector3;
}  // namespace

const WorldPlayerState* SelectMarkingTarget(const WorldState& world,
                                            const WorldPlayerState& self,
                                            const EffectivePlayerTactics* tactics) {
  if (tactics != nullptr && tactics->mark_target) {
    const WorldPlayerState* marked = FindPlayer(world, *tactics->mark_target);
    if (marked != nullptr && marked->active && marked->side != self.side) return marked;
  }
  return NearestOpponentToBall(world, self.side);
}

Vector3 CalculateDefendPosition(const WorldState& world, const WorldPlayerState& self,
                                const WorldPlayerState& opponent) {
  const float defend = static_cast<float>(TeamOf(world, self.side).defending_direction);
  const Vector3 goal(defend * world.pitch.half_length(), 0.0f, 0.0f);
  const Vector3 opponent_position = opponent.position;
  const Vector3 opponent_to_goal = goal - opponent_position;
  const Vector3 midpoint = self.position * 0.5f + opponent_position * 0.5f;
  const Vector3 perpendicular =
      (self.position - midpoint).GetRotated2D(0.5f * football::math::pi);
  float u = 0.0f;
  Intersection(opponent_position, goal, midpoint, midpoint + perpendicular, u);
  u = clamp(u, 0.0f, 1.0f);
  Vector3 target = opponent_position + (goal - opponent_position) * u;
  target += opponent_to_goal.GetNormalized(Vector3(0)) * 0.8f;
  target += opponent.velocity * 0.14f;
  return target;
}

Vector3 ApplyDefensiveComponent(const WorldState& world, const WorldPlayerState& self,
                                const WorldPlayerState& opponent,
                                const Vector3& desired, float bias) {
  if (IsKeeper(self)) return desired;
  const float defend = static_cast<float>(TeamOf(world, self.side).defending_direction);
  const Vector3 goal(defend * world.pitch.half_length(), 0.0f, 0.0f);
  const Vector3 opponent_position = opponent.position + opponent.velocity * 0.5f;

  constexpr float kPossessionShootThreshold = 24.0f;
  constexpr float kGenericShootThreshold = 8.0f;
  constexpr float kMinDistance = 0.4f;
  constexpr float kBufferDistance = 4.0f;

  float shoot_threshold = kGenericShootThreshold;
  const WorldPlayerState* holder = BallHolder(world, OtherSide(self.side));
  if (holder != nullptr && holder->id == opponent.id)
    shoot_threshold = kPossessionShootThreshold;

  const float opponent_to_goal = (goal - opponent_position).GetLength();
  const float opponent_to_threshold =
      clamp(opponent_to_goal - shoot_threshold, kMinDistance, world.pitch.half_length());
  const Vector3 shooting_point = opponent_position +
      (goal - opponent_position).GetNormalized(Vector3(0)) * opponent_to_threshold;

  Vector3 defend_position = desired;
  const Vector3 me_to_threshold = shooting_point - desired;
  const float me_distance = me_to_threshold.GetLength();
  const float slacked = me_distance - (opponent_to_threshold - kBufferDistance);
  if (slacked > 0.0f)
    defend_position = desired + me_to_threshold.GetNormalized(Vector3(0)) *
                                    clamp(slacked, 0.0f, me_distance);

  const Vector3 actual = self.position + self.velocity * 0.14f;
  const float actual_slacked = (shooting_point - actual).GetLength() - opponent_to_threshold;
  if (actual_slacked > 0.0f)
    defend_position += (goal - defend_position).GetNormalized(Vector3(0)) * actual_slacked * 0.7f;

  return desired * (1.0f - bias) + defend_position * bias;
}

}  // namespace football::ai::eliza
