// Ported from the legacy GRF support-position force field
// (ElizaController::GetSupportPosition_ForceField) and
// GetForceFieldMovement. All Player*/MentalImage* queries became PlayerId and
// public Observation values; the numerical structure is preserved.
#include "support_position.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

#include "eliza_observation.hpp"
#include "football/foundation/math/scalar.hpp"

namespace football::ai::eliza {
namespace {
using football::math::clamp;
using football::math::NormalizedClamp;
using football::player::kInvalidPlayerId;
}  // namespace

football::math::Vector3 ApplyForceField(const std::vector<ForceSpot>& field,
                                        const football::math::Vector3& current,
                                        float damping_distance,
                                        float max_velocity) {
  football::math::Vector3 cumulative;
  float accumulated_force = 0.0f;
  for (const ForceSpot& spot : field) {
    const float distance = (spot.origin - current).GetLength();
    float intensity = 1.0f;
    if (!spot.constant) {
      intensity = clamp(1.0f - distance / spot.scale, 0.0f, 1.0f);
      if (spot.exp != 1.0f) intensity = std::pow(intensity, spot.exp);
    }
    if (intensity <= 0.0f) continue;
    football::math::Vector3 direction = (spot.origin - current).GetNormalized(football::math::Vector3(0));
    if (!spot.attract) {
      direction = -direction;
    } else if (distance < damping_distance) {
      direction *= distance / damping_distance;
    }
    const float force = spot.power * intensity;
    cumulative += direction * force;
    accumulated_force += force;
  }
  if (accumulated_force == 0.0f) return football::math::Vector3(0);
  return (cumulative / accumulated_force) * max_velocity;
}

football::math::Vector3 CalculateSupportPosition(
    const WorldState& world, const football::math::Vector3& anchor,
    football::player::PlayerId player, float /*mindset*/, bool make_run,
    const ElizaParameters& parameters) {
  const WorldPlayerState* self = FindPlayer(world, player);
  if (self == nullptr || !self->active) return anchor;
  const football::sim::TeamSide side = self->side;
  const football::sim::TeamSide opposing = OtherSide(side);
  const football::math::Vector3 current = self->position + self->velocity * 0.1f;
  const WorldPlayerState* main = BallHolder(world, side);
  const football::math::Vector3 main_position =
      main != nullptr ? main->position + main->velocity * 0.1f : current;
  const float possession = TeamOf(world, side).fading_possession_amount;
  const float attack_direction =
      -static_cast<float>(TeamOf(world, side).defending_direction);

  float opponent_repel = 0.3f;
  switch (self->role) {
    case e_PlayerRole_CB:
    case e_PlayerRole_LB:
    case e_PlayerRole_RB:
      opponent_repel *= 2.2f;
      break;
    case e_PlayerRole_DM:
      opponent_repel *= 2.0f;
      break;
    case e_PlayerRole_CM:
    case e_PlayerRole_LM:
    case e_PlayerRole_RM:
      opponent_repel *= 1.6f;
      break;
    case e_PlayerRole_AM:
      opponent_repel *= 1.2f;
      break;
    default:
      break;
  }

  constexpr float kTeammateRepel = 0.4f;
  constexpr float kBallRepel = 1.0f;
  constexpr float kRunWeight = 1.0f;
  constexpr float kFlockWeight = 0.45f;
  constexpr float kWebScale = 0.75f;

  std::vector<ForceSpot> field;
  {
    ForceSpot base;
    base.origin = anchor;
    base.attract = true;
    base.constant = true;
    base.power = 0.7f * (0.3f + 0.7f * NormalizedClamp((anchor - current).GetLength(), 0.0f, 20.0f));
    field.push_back(base);
  }
  if (make_run) {
    ForceSpot run;
    run.origin = football::math::Vector3(attack_direction * world.pitch.half_length(),
                                         current.coords[1] * 0.5f, 0.0f);
    run.attract = true;
    run.constant = true;
    run.power = 2.0f * kRunWeight;
    field.push_back(run);
  }
  {
    std::vector<const WorldPlayerState*> opponents;
    const football::math::Vector3 focus = main_position * 0.3f + current * 0.7f;
    ClosestPlayers(world, opposing, focus, 3, false, kInvalidPlayerId, opponents);
    for (const WorldPlayerState* opponent : opponents) {
      const football::math::Vector3 opponent_position =
          opponent->position + opponent->velocity * 0.1f;
      ForceSpot repel;
      repel.origin = opponent_position +
                     (opponent_position - main_position).GetNormalized(football::math::Vector3(0)) * 2.0f;
      repel.attract = false;
      repel.power = opponent_repel;
      repel.scale = make_run ? 2.0f : 5.0f;
      repel.exp = 0.7f;
      if (make_run) repel.power *= 0.5f;
      field.push_back(repel);
    }
  }
  if (possession >= 1.02f) {
    std::vector<const WorldPlayerState*> teammates;
    ClosestPlayers(world, side, current, 6, false, player, teammates);
    for (const WorldPlayerState* teammate : teammates) {
      ForceSpot repel;
      repel.origin = teammate->position + teammate->velocity * 0.1f;
      repel.attract = false;
      repel.power = kTeammateRepel;
      repel.scale = 14.0f * kWebScale;
      repel.exp = 1.0f;
      field.push_back(repel);
    }
  }
  if (main != self && possession >= 1.06f) {
    for (std::size_t i = 0; i < kWorldBallPredictionCount; ++i) {
      ForceSpot repel;
      repel.attract = false;
      repel.power = kBallRepel;
      repel.scale = 2.0f;
      repel.exp = 0.5f;
      repel.origin = world.ball_prediction[i];
      field.push_back(repel);
    }
  }
  if (main != nullptr && main != self) {
    ForceSpot flock;
    flock.origin = main_position;
    flock.attract = true;
    flock.power = kFlockWeight;
    flock.scale = 28.0f * kWebScale;
    flock.exp = 1.0f;
    field.push_back(flock);
    ForceSpot spacing = flock;
    spacing.attract = false;
    spacing.scale = 16.0f * kWebScale;
    field.push_back(spacing);
  }

  football::math::Vector3 result =
      current + ApplyForceField(field, current, parameters.damping_distance,
                                parameters.sprint_velocity);
  const float offside_x = OffsideLineX(world, side);
  result.coords[0] = ApplyOffsideConstraint(result.coords[0], offside_x, attack_direction,
                                            0.08f, world.pitch.half_length());
  result.coords[1] = clamp(result.coords[1], -world.pitch.half_width(), world.pitch.half_width());
  return result;
}

}  // namespace football::ai::eliza
