#ifndef FOOTBALL_BALL_BALL_CONTACT_HPP
#define FOOTBALL_BALL_BALL_CONTACT_HPP

// Private to football::ball. Owns the discrete goal-geometry contact
// calculations (posts, crossbar, side/rear/top netting). The surrounding free
// flight integration lives in ball_physics.hpp/.cpp.

#include "football/ball/ball_config.hpp"
#include "football/ball/ball_environment.hpp"
#include "foundation/math/vector3.hpp"
#include "model/pitch.hpp"

namespace football::ball::detail {

// Resolves posts/crossbar collisions in-place. Mirrors the legacy computation,
// including the order of position projection and momentum reflection.
void ResolveWoodwork(blunted::Vector3& position, blunted::Vector3& momentum,
                     const football::model::Pitch& pitch,
                     const football::ball::BallConfig& config);

// Resolves side/rear/top goal netting in-place when the ball is behind the
// goal line inside the goal mouth.
void ResolveNetting(blunted::Vector3& position, blunted::Vector3& momentum,
                    const football::model::Pitch& pitch,
                    const football::ball::BallConfig& config,
                    const football::ball::BallEnvironment& environment);

}  // namespace football::ball::detail

#endif  // FOOTBALL_BALL_BALL_CONTACT_HPP
