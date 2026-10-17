// Ported from the legacy on-the-ball decision path
// (ElizaController::GetOnTheBallCommands, _GetPassingOdds, _AddPass and
// GetBestDribbleMovement) plus the loose-ball Trap/Dribble and Sliding gates
// from PlayerController. Player*/MentalImage*/RNG became public Observation
// values and a fixed deterministic threshold.
#include "on_ball.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "eliza_geometry.hpp"
#include "eliza_observation.hpp"
#include "football/foundation/math/scalar.hpp"
#include "off_ball.hpp"
#include "support_position.hpp"

namespace football::ai::eliza {
namespace {
using football::math::clamp;
using football::math::curve;
using football::math::NormalizedClamp;
using football::math::Vector3;
using football::player::kInvalidPlayerId;

// The legacy tactical situation (forward space / space / forward) projected
// onto the public observation.
float SpaceRating(const WorldState& world, const WorldPlayerState& self) {
  float nearest = 40.0f;
  for (const WorldPlayerState& opponent : world.players) {
    if (!opponent.active || opponent.side == self.side) continue;
    nearest = std::min(nearest, (opponent.position - self.position).Get2D().GetLength());
  }
  return NormalizedClamp(nearest, 0.0f, 20.0f);
}

float ForwardRating(const WorldState& world, const WorldPlayerState& self) {
  const float attack =
      -static_cast<float>(TeamOf(world, self.side).defending_direction);
  return NormalizedClamp(self.position.coords[0] * attack,
                         -world.pitch.half_length(), world.pitch.half_length()) *
             0.5f +
         0.5f;
}

float TacticalRating(const WorldState& world, const WorldPlayerState& self,
                     float mindset) {
  const float forward = ForwardRating(world, self);
  const float space = SpaceRating(world, self);
  const float forward_space = space * (0.5f + 0.5f * forward);
  const float forward_weight = 2.0f + mindset * 6.0f;
  const float weight_sum = 0.4f + 0.3f + forward_weight;
  return (forward_space * 0.4f + space * 0.3f + forward * forward_weight) /
         weight_sum;
}
}  // namespace

float PassingOdds(const WorldState& world, const WorldPlayerState& self,
                  const Vector3& origin, const Vector3& target,
                  ControlAction pass_type, float ball_velocity_multiplier) {
  constexpr float kSecondScale = 1.0f;
  constexpr float kSprintVelocity = 8.0f;
  float danger = 0.0f;
  for (const WorldPlayerState& opponent : world.players) {
    if (!opponent.active || opponent.side == self.side) continue;
    const Vector3 opponent_position = opponent.position + opponent.velocity * 0.2f;
    float u = 0.0f;
    const float opponent_distance =
        DistanceToPoint(origin, target, opponent_position, u);
    if (!(u >= 0.0f && u <= 1.2f)) continue;
    if (pass_type == ControlAction::HighPass && !(u < 0.2f || u > 0.65f)) continue;
    const float clamped = clamp(u, 0.0f, 1.0f);
    const Vector3 intersect = origin * (1.0f - clamped) + target * clamped;
    const float opponent_time = (opponent_distance + 1.0f) / kSprintVelocity;
    const float penalty =
        (pass_type == ControlAction::HighPass && u > 0.5f) ? 2.5f : 0.0f;
    float ball_time =
        0.7f + (intersect - origin).GetLength() * clamped * 0.03f + penalty;
    ball_time *= 1.0f / ball_velocity_multiplier;
    danger += clamp(ball_time - opponent_time + kSecondScale * 0.5f, 0.0f,
                    kSecondScale);
  }
  if (pass_type == ControlAction::HighPass) danger += 0.4f;
  danger = NormalizedClamp(danger, 0.0f, kSecondScale);
  return 1.0f - danger;
}

OnBallDecision DecideOnBall(const WorldState& world, const TacticalBoard& board,
                            const WorldPlayerState& self,
                            const ElizaParameters& parameters,
                            float possession_ms) {
  (void)board;
  (void)parameters;
  OnBallDecision decision;
  const float mindset = MindSet(self.role);
  const float attack =
      -static_cast<float>(TeamOf(world, self.side).defending_direction);
  const Vector3 goal(attack * world.pitch.half_length(), 0.0f, 0.0f);

  // Shoot when the position and the lane are good enough. The legacy draw is
  // replaced by a fixed odds threshold so the policy stays deterministic.
  const Vector3 ideal_shot_position(
      (world.pitch.half_length() - 7.0f) * attack, 0.0f, 0.0f);
  float ideal_shot_factor = 1.0f - NormalizedClamp(
      (ideal_shot_position - self.position).Get2D().GetLength(), 0.0f, 16.0f);
  ideal_shot_factor = curve(ideal_shot_factor, 1.0f);
  if (ideal_shot_factor > 0.1f) {
    const Vector3 posts[3] = {
        Vector3((world.pitch.half_length() + 1.0f) * attack, -3.6f, 0.0f),
        Vector3((world.pitch.half_length() + 1.0f) * attack, 0.0f, 0.0f),
        Vector3((world.pitch.half_length() + 1.0f) * attack, 3.6f, 0.0f)};
    float best_odds = 0.0f;
    Vector3 best_point = posts[1];
    for (const Vector3& point : posts) {
      const float odds =
          PassingOdds(world, self, self.position, point, ControlAction::Shoot, 3.0f);
      if (odds > best_odds) {
        best_odds = odds;
        best_point = point;
      }
    }
    if (std::sqrt(best_odds) > 0.30f) {
      decision.action = ControlAction::Shoot;
      decision.target_position = best_point;
      decision.power =
          clamp(0.5f + (goal - self.position).GetLength() / 40.0f, 0.5f, 1.0f);
      return decision;
    }
  }

  // Pass selection.
  const float self_rating = TacticalRating(world, self, mindset);
  const float improvement_threshold = 0.06f * (1.0f - mindset);
  const float tactical_diff_weight = 1.0f + mindset * mindset * 10.0f;
  const float pass_weight = 1.0f;
  // A long possession lowers the release threshold, as the legacy
  // longPossessionFactor did. `possession_ms` is policy memory, not an observation.
  const float long_possession =
      std::pow(NormalizedClamp(possession_ms, 0.0f, 5000.0f), 2.0f);
  const float pass_minimum = 0.2f * (1.0f - mindset) - long_possession * 0.1f;
  const float pass_threshold = 0.1f - long_possession * 0.05f;
  const float weight_sum = tactical_diff_weight + pass_weight;

  const WorldPlayerState* best_target = nullptr;
  ControlAction best_type = ControlAction::None;
  float best_total = 0.0f;

  for (const WorldPlayerState& mate : world.players) {
    if (!mate.active || mate.side != self.side || mate.id == self.id) continue;
    const float mate_rating = TacticalRating(world, mate, mindset);
    if (mate_rating <= self_rating + improvement_threshold) continue;
    const float distance = (mate.position - self.position).Get2D().GetLength();
    if (distance < 3.0f || distance > 45.0f) continue;

    const float estimated_time = 0.7f + distance * 0.03f;
    Vector3 target = mate.position + mate.velocity * clamp(estimated_time, 0.0f, 0.5f);
    const float short_odds =
        PassingOdds(world, self, self.position, target, ControlAction::ShortPass, 1.0f);
    const float high_odds =
        PassingOdds(world, self, self.position, target, ControlAction::HighPass, 1.0f);
    Vector3 long_target =
        target + Vector3(attack * distance * 0.2f, 0.0f, 0.0f);
    const float long_odds =
        PassingOdds(world, self, self.position, long_target, ControlAction::LongPass, 1.0f);

    // Short ties win, exactly as the legacy chain ordered them.
    ControlAction type = ControlAction::ShortPass;
    float pass_rating = short_odds;
    if (short_odds < long_odds || short_odds < high_odds) {
      if (long_odds >= high_odds) {
        type = ControlAction::LongPass;
        pass_rating = long_odds;
      } else {
        type = ControlAction::HighPass;
        pass_rating = high_odds;
      }
    }
    if (pass_rating <= pass_minimum) continue;

    const float tactical_diff = mate_rating - self_rating;
    const float total = (tactical_diff * tactical_diff_weight + pass_rating * pass_weight) /
                        weight_sum;
    if (total > best_total && total > pass_threshold) {
      best_total = total;
      best_target = &mate;
      best_type = type;
    }
  }

  if (best_target != nullptr) {
    decision.action = best_type;
    decision.target_player = best_target->id;
    return decision;
  }
  return decision;  // keep dribbling
}

Vector3 DribbleMovement(const WorldState& world, const WorldPlayerState& self,
                        const ElizaParameters& parameters) {
  const float attack =
      -static_cast<float>(TeamOf(world, self.side).defending_direction);
  const Vector3 current = self.position + self.velocity * 0.25f;
  const float near_backline =
      NormalizedClamp(std::fabs(self.position.coords[0]) / world.pitch.half_length(),
                      0.0f, 1.0f);
  float centre_inverse = 1.0f - std::pow(near_backline, 2.0f);
  centre_inverse *= 0.5f;
  const float mindset = MindSet(self.role);
  const Vector3 goal(attack * world.pitch.half_length(),
                     self.position.coords[1] * 0.5f * centre_inverse, 0.0f);
  const float offense_factor = 0.7f + 0.5f * 0.05f + mindset * 0.05f;

  std::vector<ForceSpot> field;
  std::vector<const WorldPlayerState*> opponents;
  ClosestPlayers(world, OtherSide(self.side), self.position, 5, false,
                 kInvalidPlayerId, opponents);
  for (const WorldPlayerState* opponent : opponents) {
    ForceSpot spot;
    spot.origin = opponent->position + opponent->velocity * 0.25f;
    spot.attract = false;
    spot.power = 2.0f;
    spot.scale = 10.0f;
    spot.exp = 1.0f;
    field.push_back(spot);
  }
  {
    ForceSpot sideline;
    sideline.origin = Vector3(
        self.position.coords[0],
        (world.pitch.half_width() + 5.0f) * football::math::signSide(self.position.coords[1]),
        0.0f);
    sideline.attract = false;
    sideline.power = 4.0f;
    sideline.scale = 20.0f;
    sideline.exp = 0.7f;
    field.push_back(sideline);
    ForceSpot backline = sideline;
    backline.origin = Vector3(
        (world.pitch.half_length() + 5.0f) * football::math::signSide(self.position.coords[0]),
        self.position.coords[1], 0.0f);
    field.push_back(backline);
  }
  {
    ForceSpot attract;
    attract.origin = goal;
    attract.attract = true;
    attract.constant = true;
    attract.power = offense_factor;
    field.push_back(attract);
  }
  return ApplyForceField(field, current, 1.0f, parameters.sprint_velocity);
}

OnBallDecision DecideLooseBall(const WorldState& world,
                               const WorldPlayerState& self,
                               const ElizaParameters& parameters) {
  (void)parameters;
  OnBallDecision decision;
  if (world.ball_retainer) return decision;
  const float distance = (self.position - world.ball_position).Get2D().GetLength();
  if (distance > 2.5f || world.ball_position.coords[2] > 1.5f) return decision;
  decision.action = world.ball_velocity.Get2D().GetLength() > 2.0f
                        ? ControlAction::Trap
                        : ControlAction::Dribble;
  return decision;
}

ControlAction DecideTackle(const WorldState& world,
                           const WorldPlayerState& self) {
  if (TeamOf(world, self.side).has_possession) return ControlAction::None;
  const WorldPlayerState* holder = BallHolder(world, OtherSide(self.side));
  if (holder == nullptr) return ControlAction::None;
  if ((self.position - holder->position).Get2D().GetLength() > 2.2f)
    return ControlAction::None;
  const float ball_distance = (self.position - world.ball_position).Get2D().GetLength();
  if (ball_distance < 0.4f || ball_distance > 2.0f) return ControlAction::None;
  return ControlAction::Tackle;
}

}  // namespace football::ai::eliza
