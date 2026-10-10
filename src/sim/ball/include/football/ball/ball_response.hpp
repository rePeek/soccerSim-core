#ifndef FOOTBALL_BALL_BALL_RESPONSE_HPP
#define FOOTBALL_BALL_BALL_RESPONSE_HPP

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_state.hpp"
#include "football/ball/collider.hpp"
#include "model/ball_config.hpp"

namespace football::ball {

// Spherical inertia factor; thin-shell approximation. Keep it internal until
// different ball materials actually need it in BallConfig.
inline constexpr float kBallInertiaFactor = 2.0f / 3.0f;

float BallInertia(const football::model::BallConfig& config);

// Bottom of the response stack: apply an impulse at a world-space point and
// change both velocity and angular_velocity. No contact judgement, friction or
// restitution lives here; dt is never applied to an impulse.
BallState ApplyImpulseAtPoint(const BallState& ball,
                              const blunted::Vector3& impulse,
                              const blunted::Vector3& world_point,
                              const football::model::BallConfig& config);

// Passive contact response: normal impulse from restitution plus
// Coulomb-capped tangential friction impulse from the contact-point relative
// velocity, then ApplyImpulseAtPoint. BallState.position is the authoritative
// center at response time (possibly projected out of initial penetration).
// `collider_velocity` is the collider contact velocity (zero for static).
BallState ResolveContact(const BallState& ball, const BallContact& contact,
                         const ContactMaterial& material,
                         const blunted::Vector3& collider_velocity,
                         const football::model::BallConfig& config);

// Explicit response diagnostics: a separating/resting contact has jn == 0.
struct BallContactResponse {
  BallState state;
  float normal_impulse = 0.0f; // N*s, not a per-tick velocity change
  blunted::Vector3 tangential_impulse{0};
};
BallContactResponse ResolveContactResponse(
    const BallState& ball, const BallContact& contact,
    const ContactMaterial& material, const blunted::Vector3& collider_velocity,
    const football::model::BallConfig& config);

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_RESPONSE_HPP