#ifndef FOOTBALL_SIM_PLAYER_FOOT_IK_HPP
#define FOOTBALL_SIM_PLAYER_FOOT_IK_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "football/foundation/math/vector3.hpp"

// P3b core: support-foot anchors and a deterministic two-bone leg solve.
//
// This module is deliberately independent of Animation, Contact and Runtime.
// The caller supplies world-space ankle positions and support flags; it receives
// world-space knee/ankle targets and a released flag when the anchor is outside
// the leg's reach. Pose/Final Pose integration consumes these targets.
enum class FootContactPhase : std::uint8_t {
  Swing,
  Planting,
  Planted,
  Releasing,
};

struct PlayerFootPlantState {
  bool left_planted = false;
  bool right_planted = false;
  football::math::Vector3 left_anchor = football::math::Vector3(0);
  football::math::Vector3 right_anchor = football::math::Vector3(0);
  FootContactPhase left_phase = FootContactPhase::Swing;
  FootContactPhase right_phase = FootContactPhase::Swing;
};

struct PlayerFootPlantInput {
  football::math::Vector3 left_ankle = football::math::Vector3(0);
  football::math::Vector3 right_ankle = football::math::Vector3(0);
  bool left_support = false;
  bool right_support = false;
  // Feet at or below ground + this tolerance may plant.
  float plant_height = 0.16f;
};

struct PlayerFootIkTargets {
  bool has_left = false;
  bool has_right = false;
  football::math::Vector3 left = football::math::Vector3(0);
  football::math::Vector3 right = football::math::Vector3(0);
};

// STRIDE-0: the slide the final pose has to remove, measured on the ankle the
// animation itself produced. A planted foot whose ankle has drifted from its
// anchor is exactly the sliding step this pipeline must not ship. Only the
// horizontal distance counts: lifting a foot is not sliding.
struct PlayerFootPlantSlip {
  float left = 0.0f;
  float right = 0.0f;
};

inline PlayerFootPlantSlip MeasureFootPlantSlip(
    const PlayerFootPlantState& plant,
    const football::math::Vector3& left_ankle,
    const football::math::Vector3& right_ankle) {
  PlayerFootPlantSlip slip;
  if (plant.left_planted) {
    slip.left = (left_ankle - plant.left_anchor).Get2D().GetLength();
  }
  if (plant.right_planted) {
    slip.right = (right_ankle - plant.right_anchor).Get2D().GetLength();
  }
  return slip;
}

// The support authority: a planted foot is released from whatever the gait
// wanted and targets the anchor it was planted on. This is the only place
// support overrides the stride, so shortening a stride can never stretch or
// drag a stance phase, and no two systems write the same foot.
inline PlayerFootIkTargets ApplyFootPlantLocks(
    const PlayerFootPlantState& plant, PlayerFootIkTargets targets) {
  if (plant.left_planted) {
    targets.has_left = true;
    targets.left = plant.left_anchor;
  }
  if (plant.right_planted) {
    targets.has_right = true;
    targets.right = plant.right_anchor;
  }
  return targets;
}

struct PlayerTwoBoneResult {
  football::math::Vector3 knee = football::math::Vector3(0);
  football::math::Vector3 ankle = football::math::Vector3(0);
  bool reachable = false;
  bool released = false;
};

class PlayerFootIk final {
 public:
  // Keeps the current knee bend plane, preserves both bone lengths and clamps
  // an over-extended target to the leg's reachable sphere.
  static inline PlayerTwoBoneResult Solve(
      const football::math::Vector3& hip,
      const football::math::Vector3& knee,
      const football::math::Vector3& ankle,
      const football::math::Vector3& target) {
    using football::math::Vector3;
    PlayerTwoBoneResult result;
    const float upper = (knee - hip).GetLength();
    const float lower = (ankle - knee).GetLength();
    if (upper <= 1e-6f || lower <= 1e-6f) {
      result.knee = knee;
      result.ankle = ankle;
      return result;
    }
    const float max_reach = upper + lower - 1e-4f;
    const float min_reach = std::fabs(upper - lower) + 1e-4f;
    Vector3 goal = target;
    Vector3 direction = goal - hip;
    float distance = direction.GetLength();
    if (distance <= 1e-6f) {
      goal = hip + Vector3(0, 0, -max_reach);
    } else if (distance < min_reach) {
      goal = hip + direction * (min_reach / distance);
    } else if (distance > max_reach) {
      goal = hip + direction * (max_reach / distance);
    }
    result.released = (goal - target).GetLength() > 1e-5f;
    direction = goal - hip;
    distance = direction.GetLength();
    if (distance <= 1e-6f) {
      result.knee = knee;
      result.ankle = ankle;
      return result;
    }
    const Vector3 forward = direction * (1.0f / distance);
    Vector3 bend = knee - hip;
    bend -= forward * bend.GetDotProduct(forward);
    if (bend.GetLength() <= 1e-5f) {
      bend = forward.GetCrossProduct(Vector3(0, 0, 1));
      if (bend.GetLength() <= 1e-5f)
        bend = forward.GetCrossProduct(Vector3(0, 1, 0));
    }
    bend = bend.GetNormalized(Vector3(0, 0, -1));
    const float along = (upper * upper - lower * lower + distance * distance) /
                        (2.0f * distance);
    const float height_sq = std::max(0.0f, upper * upper - along * along);
    const float height = std::sqrt(height_sq);
    result.knee = hip + forward * along + bend * height;
    result.ankle = goal;
    result.reachable = true;
    return result;
  }

  // Updates support anchors. A planted foot keeps its first world anchor until
  // support is lost or the pose leaves the ground band.
  static inline PlayerFootPlantState Update(
      const PlayerFootPlantState& previous, const PlayerFootPlantInput& input) {
    PlayerFootPlantState next = previous;
    const auto update = [&](bool support, const football::math::Vector3& ankle,
                            bool& planted, football::math::Vector3& anchor,
                            FootContactPhase& phase) {
      const bool in_band = ankle.coords[2] <= input.plant_height;
      if (!support || !in_band) {
        const bool was_planted = planted;
        planted = false;
        anchor = football::math::Vector3(0);
        phase = was_planted ? FootContactPhase::Releasing
                            : FootContactPhase::Swing;
        return;
      }
      if (!planted) {
        planted = true;
        anchor = ankle;
        phase = FootContactPhase::Planting;
        return;
      }
      phase = FootContactPhase::Planted;
    };
    update(input.left_support, input.left_ankle, next.left_planted,
           next.left_anchor, next.left_phase);
    update(input.right_support, input.right_ankle, next.right_planted,
           next.right_anchor, next.right_phase);
    return next;
  }
};

#endif  // FOOTBALL_SIM_PLAYER_FOOT_IK_HPP
