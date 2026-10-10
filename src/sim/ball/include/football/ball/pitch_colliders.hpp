#ifndef FOOTBALL_BALL_PITCH_COLLIDERS_HPP
#define FOOTBALL_BALL_PITCH_COLLIDERS_HPP

#include <vector>

#include "football/ball/collider.hpp"

namespace football::model {
class Pitch;
class BallConfig;
}  // namespace football::model

namespace football::ball {

// Fixed, deterministic static collision world for the pitch: one ground plane,
// four goal posts and two crossbars. Construct once per match, never per tick.
//
// Stable ids (also the deterministic tie-break order):
//   1      ground
//   2, 3   left goal posts (y = -/+ goal half width)
//   4, 5   right goal posts
//   6, 7   left / right crossbars
//
// The ground material reads its restitution from the ball config; posts and
// bars use a rigid steel material. Friction fields are populated but are not
// yet applied by the response kernel (see P3b).
std::vector<ColliderMotion> BuildPitchColliders(const football::model::Pitch& pitch,
                                                const football::model::BallConfig& ball);

}  // namespace football::ball

#endif  // FOOTBALL_BALL_PITCH_COLLIDERS_HPP