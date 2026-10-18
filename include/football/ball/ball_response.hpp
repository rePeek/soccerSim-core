#pragma once

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_model.hpp"
#include "football/ball/ball_state.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ball {

inline constexpr float kBallInertiaFactor = 2.0f / 3.0f;

float BallInertia(const football::ball::BallModel& config);

BallState ApplyImpulseAtPoint(const BallState& ball,
                              const football::math::Vector3& impulse,
                              const football::math::Vector3& world_point,
                              const football::ball::BallModel& config);

BallState ResolveContact(const BallState& ball, const BallContact& contact,
                         const football::geometry::ContactMaterial& material,
                         const football::math::Vector3& collider_velocity,
                         const football::ball::BallModel& config);

struct BallContactResponse {
  BallState state;
  float normal_impulse = 0.0f;
  football::math::Vector3 tangential_impulse{0};
};
BallContactResponse ResolveContactResponse(
    const BallState& ball, const BallContact& contact,
    const football::geometry::ContactMaterial& material, const football::math::Vector3& collider_velocity,
    const football::ball::BallModel& config);

}