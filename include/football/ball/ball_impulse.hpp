#ifndef FOOTBALL_BALL_IMPULSE_HPP
#define FOOTBALL_BALL_IMPULSE_HPP
#include "football/foundation/math/vector3.hpp"
namespace football::ball {
// Pure physical input. No identity, action, touch type, or rules dependency.
struct BallImpulse {
  football::math::Vector3 impulse = football::math::Vector3(0); // N*s
  football::math::Vector3 contact_point = football::math::Vector3(0); // world-space ball surface
};
}
#endif
