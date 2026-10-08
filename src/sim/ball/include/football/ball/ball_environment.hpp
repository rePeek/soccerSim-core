#ifndef FOOTBALL_BALL_BALL_ENVIRONMENT_HPP
#define FOOTBALL_BALL_BALL_ENVIRONMENT_HPP

#include "foundation/math/vector3.hpp"

namespace football::ball {

// External physical facts needed for one Step/Predict call, supplied by the
// caller each tick. Ball neither retains nor mutates this value and never
// asks "who" produced a fact: it only receives physical input.
//
// `ball_in_goal` is a transitional rule fact consumed by the legacy netting
// calculation. It is intentionally kept as the first member so the existing
// `{true}` / `{ball_in_goal}` aggregate initializers remain valid until the
// referee owns that fact and can be replaced by explicit physical geometry.
struct BallEnvironment {
  bool ball_in_goal = false;
  blunted::Vector3 wind_velocity;          // reserved; currently unused
  bool net_collision_enabled = true;       // reserved; currently unused
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_ENVIRONMENT_HPP
