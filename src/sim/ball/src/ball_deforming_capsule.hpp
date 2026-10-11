#ifndef FOOTBALL_BALL_DEFORMING_CAPSULE_HPP
#define FOOTBALL_BALL_DEFORMING_CAPSULE_HPP
#include "football/ball/ball_contact.hpp"
namespace football::ball::detail {
// Endpoint-linear, radius-linear capsule motion. No actor/animation dependency.
std::optional<BallContact> SweepDeformingCapsule(const BallState& ball, float ball_radius,
    const ColliderMotion& collider, const Capsule& start, const Capsule& end, float dt);
}
#endif
