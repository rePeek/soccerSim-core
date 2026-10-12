#ifndef FOOTBALL_BALL_BALL_ENVIRONMENT_HPP
#define FOOTBALL_BALL_BALL_ENVIRONMENT_HPP

#include "football/foundation/math/vector3.hpp"

namespace football::ball {

// External physical facts needed for one Step/Predict call, supplied by the
// caller each tick. Ball neither retains nor mutates this value and never
// asks "who" produced a fact: it only receives physical input. Reserved
// members are placeholders for future physical facts, not rule/actor state.
struct BallEnvironment {
  blunted::Vector3 wind_velocity;          // reserved; currently unused
  bool net_collision_enabled = true;       // reserved; currently unused
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_ENVIRONMENT_HPP