#ifndef FOOTBALL_AI_ELIZA_SUPPORT_POSITION_HPP
#define FOOTBALL_AI_ELIZA_SUPPORT_POSITION_HPP

#include <vector>

#include "eliza_parameters.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai::eliza {

// One tactical attraction/repulsion source. `constant` keeps full intensity at
// any distance; otherwise the intensity decays over `scale` and is raised to
// `exp`. Ported from the legacy ForceSpot/GetForceFieldMovement pair.
struct ForceSpot {
  football::math::Vector3 origin;
  bool attract = true;
  bool constant = false;
  float power = 1.0f;
  float scale = 10.0f;
  float exp = 1.0f;
};

// Normalised weighted force direction times the sprint speed; zero when the
// field is empty or balanced.
football::math::Vector3 ApplyForceField(const std::vector<ForceSpot>& field,
                                        const football::math::Vector3& current,
                                        float damping_distance,
                                        float max_velocity);

// Support Position Force Field: base anchor plus opponent/teammate/ball
// avoidance and flocking to the ball holder, kept onside and on the pitch.
football::math::Vector3 CalculateSupportPosition(
    const WorldState& world, const football::math::Vector3& anchor,
    football::player::PlayerId player, float mindset, bool make_run,
    const ElizaParameters& parameters);

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_SUPPORT_POSITION_HPP
