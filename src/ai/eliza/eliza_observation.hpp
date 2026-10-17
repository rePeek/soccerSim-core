#ifndef FOOTBALL_AI_ELIZA_OBSERVATION_HPP
#define FOOTBALL_AI_ELIZA_OBSERVATION_HPP

#include <algorithm>
#include <cstddef>
#include <vector>

#include "football/foundation/math/scalar.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai::eliza {

// Read-only queries over the public Observation. They replace the legacy
// `AI_GetClosestPlayer`/`GetOffsideLine` helpers without touching Match, Team,
// Player or MentalImage.
using football::math::Vector3;
using football::player::PlayerId;
using football::sim::TeamSide;

inline TeamSide OtherSide(TeamSide side) {
  return side == TeamSide::Home ? TeamSide::Away : TeamSide::Home;
}

inline const WorldPlayerState* FindPlayer(const WorldState& world, PlayerId id) {
  for (const WorldPlayerState& player : world.players)
    if (player.id == id) return &player;
  return nullptr;
}

inline bool IsKeeper(const WorldPlayerState& player) {
  return player.role == e_PlayerRole_GK;
}

inline const WorldTeamState& TeamOf(const WorldState& world, TeamSide side) {
  return world.teams[static_cast<unsigned>(side)];
}

// The teammate carrying the ball: explicit retainer first, then the single
// possession holder. Null when neither side has a holder.
inline const WorldPlayerState* BallHolder(const WorldState& world, TeamSide side) {
  if (world.ball_retainer) {
    const WorldPlayerState* player = FindPlayer(world, *world.ball_retainer);
    if (player != nullptr && player->side == side) return player;
  }
  for (const WorldPlayerState& player : world.players)
    if (player.active && player.side == side && player.has_possession) return &player;
  return nullptr;
}

// The opponent the ball is nearest to being contested by: the closest opponent
// to the ball. Used as the default defensive/marking target.
inline const WorldPlayerState* NearestOpponentToBall(const WorldState& world,
                                                     TeamSide side) {
  const WorldPlayerState* best = nullptr;
  float distance = 0.0f;
  for (const WorldPlayerState& player : world.players) {
    if (!player.active || player.side == side) continue;
    const float candidate = (player.position - world.ball_position).GetSquaredLength();
    if (best == nullptr || candidate < distance) {
      best = &player;
      distance = candidate;
    }
  }
  return best;
}

// Ball centre at `ms`, linearly interpolated between the runtime samples.
inline Vector3 BallAt(const WorldState& world, int ms) {
  if (ms <= 0) return world.ball_position;
  float previous_ms = 0.0f;
  Vector3 previous = world.ball_position;
  for (std::size_t i = 0; i < kWorldBallPredictionCount; ++i) {
    const float horizon = static_cast<float>(kWorldBallPredictionHorizons_ms[i]);
    if (static_cast<float>(ms) <= horizon) {
      const float t = (static_cast<float>(ms) - previous_ms) / (horizon - previous_ms);
      return previous + (world.ball_prediction[i] - previous) * t;
    }
    previous = world.ball_prediction[i];
    previous_ms = horizon;
  }
  return world.ball_prediction[kWorldBallPredictionCount - 1];
}

// Closest active players of one side to `position`. Distance then identity
// breaks ties, so the result is deterministic and independent of roster order.
inline void ClosestPlayers(const WorldState& world, TeamSide side,
                           const Vector3& position, int count, bool exclude_keeper,
                           PlayerId except,
                           std::vector<const WorldPlayerState*>& out) {
  out.clear();
  for (const WorldPlayerState& player : world.players) {
    if (!player.active || player.side != side || player.id == except) continue;
    if (exclude_keeper && IsKeeper(player)) continue;
    out.push_back(&player);
  }
  std::sort(out.begin(), out.end(),
            [&position](const WorldPlayerState* a, const WorldPlayerState* b) {
              const float da = (a->position - position).GetSquaredLength();
              const float db = (b->position - position).GetSquaredLength();
              if (da != db) return da < db;
              return a->id < b->id;
            });
  if (static_cast<int>(out.size()) > count)
    out.resize(static_cast<std::size_t>(count));
}

// Off-side line x for `attacking_side`: the second-deepest defender, the ball
// and the halfway line, whichever is most advanced. A positioning aid only;
// the referee still owns the offside decision.
inline float OffsideLineX(const WorldState& world, TeamSide attacking_side) {
  const TeamSide defending = OtherSide(attacking_side);
  const float attack_dir =
      static_cast<float>(TeamOf(world, attacking_side).defending_direction) * -1.0f;
  float deepest = 0.0f;
  float second = 0.0f;
  bool have_deepest = false;
  bool have_second = false;
  for (const WorldPlayerState& player : world.players) {
    if (!player.active || player.side != defending || IsKeeper(player)) continue;
    const float x = player.position.coords[0] * attack_dir;
    if (!have_deepest || x < deepest) {
      second = deepest;
      have_second = have_deepest;
      deepest = x;
      have_deepest = true;
    } else if (!have_second || x < second) {
      second = x;
      have_second = true;
    }
  }
  float line = 0.0f;
  if (have_second) line = second;
  else if (have_deepest) line = deepest;
  line = std::max(line, world.ball_position.coords[0] * attack_dir);
  line = std::max(line, 0.0f);
  return line * attack_dir;
}

// Keep an attacker behind the offside line with a small margin, then on pitch.
inline float ApplyOffsideConstraint(float x, float offside_x, float attack_dir,
                                    float margin, float half_length) {
  if (x * attack_dir > offside_x * attack_dir - margin)
    x = offside_x - margin * attack_dir;
  return football::math::clamp(x, -half_length, half_length);
}

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_OBSERVATION_HPP
