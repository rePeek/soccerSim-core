#ifndef FOOTBALL_BALL_DEFORMING_CAPSULE_HPP
#define FOOTBALL_BALL_DEFORMING_CAPSULE_HPP
#include "football/ball/ball_contact.hpp"
namespace football::ball::detail {
// Endpoint-linear, radius-linear capsule motion. No actor/animation dependency.
std::optional<BallContact> SweepDeformingCapsule(const BallState& ball, float ball_radius,
    const football::geometry::ColliderMotion& collider, const football::geometry::Capsule& start, const football::geometry::Capsule& end, float dt);
}
#endif
