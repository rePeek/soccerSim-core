#include "football/ball/ball_response.hpp"

namespace football::ball {

float BallInertia(const football::model::BallConfig& config) {
  return kBallInertiaFactor * config.mass() * config.radius() * config.radius();
}

BallState ApplyImpulseAtPoint(const BallState& ball, const blunted::Vector3& impulse,
                              const blunted::Vector3& world_point,
                              const football::model::BallConfig& config) {
  BallState next = ball;
  next.velocity = next.velocity + impulse * (1.0f / config.mass());

  const blunted::Vector3 r = world_point - ball.position;
  const blunted::Vector3 torque(
      r.coords[1] * impulse.coords[2] - r.coords[2] * impulse.coords[1],
      r.coords[2] * impulse.coords[0] - r.coords[0] * impulse.coords[2],
      r.coords[0] * impulse.coords[1] - r.coords[1] * impulse.coords[0]);
  next.angular_velocity = next.angular_velocity + torque * (1.0f / BallInertia(config));
  return next;
}

BallContactResponse ResolveContactResponse(const BallState& ball, const BallContact& contact,
                                            const ContactMaterial& material,
                                            const blunted::Vector3& collider_velocity,
                                            const football::model::BallConfig& config) {
  const blunted::Vector3 n = contact.normal.GetNormalized({0, 0, 1});
  const blunted::Vector3 center = ball.position;
  const blunted::Vector3 surface_point = center - n * config.radius();
  const blunted::Vector3 r = surface_point - center;

  const blunted::Vector3& omega = ball.angular_velocity;
  const blunted::Vector3 omega_cross_r(
      omega.coords[1] * r.coords[2] - omega.coords[2] * r.coords[1],
      omega.coords[2] * r.coords[0] - omega.coords[0] * r.coords[2],
      omega.coords[0] * r.coords[1] - omega.coords[1] * r.coords[0]);

  const blunted::Vector3 u = ball.velocity + omega_cross_r - collider_velocity;
  const float un = u.GetDotProduct(n);
  if (un >= 0.0f) return {ball};  // separating or resting: no new impulse

  const float mass = config.mass();
  const float jn = -mass * (1.0f + material.restitution) * un;

  const blunted::Vector3 ut = u - n * un;
  // Effective tangential inverse mass for a sphere: 1/m + R^2 / I.
  const float inverse =
      1.0f / mass + (config.radius() * config.radius()) / BallInertia(config);
  blunted::Vector3 jt = ut * (-1.0f / inverse);

  const float jt_len = jt.GetLength();
  const float mu_jn = material.friction * jn;
  if (jt_len > mu_jn) jt = jt * (mu_jn / jt_len);

  return {ApplyImpulseAtPoint(ball, n * jn + jt, surface_point, config), jn, jt};
}

BallState ResolveContact(const BallState& ball, const BallContact& contact,
                         const ContactMaterial& material,
                         const blunted::Vector3& collider_velocity,
                         const football::model::BallConfig& config) {
  return ResolveContactResponse(ball, contact, material, collider_velocity, config).state;
}

}  // namespace football::ball