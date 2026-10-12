#ifndef FOOTBALL_BALL_IMPULSE_HPP
#define FOOTBALL_BALL_IMPULSE_HPP
#include "foundation/math/vector3.hpp"
namespace football::ball {
// Pure physical input. No identity, action, touch type, or rules dependency.
struct BallImpulse {
  blunted::Vector3 impulse = blunted::Vector3(0); // N*s
  blunted::Vector3 contact_point = blunted::Vector3(0); // world-space ball surface
};
}
#endif
