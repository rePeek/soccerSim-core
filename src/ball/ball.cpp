#include "football/ball/ball.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <span>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/log/log.hpp"
#include "football/pitch/pitch.hpp"

using namespace football::log;
using namespace football::math;

namespace football::ball {

using geometry::ContactMaterial;

namespace {

// Touching distance to the grass and to the interval start: closer than this is
// "already in contact", not a new impact.
constexpr float kTouchEpsilon = 1e-4f;

// Relative slack for "the point lies on the shell": the contact normal is
// normalized in float, so the distance is only radius-accurate to a few ulps.
constexpr float kSurfaceTolerance = 1e-3f;

// ---- Motion ----

// Thin-shell sphere inertia, I = 2/3 * mass * radius^2.
float Inertia(const BallModel& model) {
  return (2.0f / 3.0f) * model.mass * model.radius * model.radius;
}

// A ball on the grass rising slower than this cannot lift more than roughly half
// a millimetre, so its tiny bounce is absorbed as rolling instead of crossing the
// grass boundary and needing a repair next tick.
constexpr float kLiftEpsilon = 0.1f;

// The ball rolls while it rests on the grass, and flies otherwise. This is the
// single definition of "on the ground and not really lifting", and it picks the
// integrator's branch.
bool IsRolling(const BallModel& model, const BallState& state) {
  return state.position.coords[2] <= model.radius + kTouchEpsilon &&
         std::fabs(state.velocity.coords[2]) <= kLiftEpsilon;
}

// Grass resists horizontal motion only: a constant deceleration plus an optional
// quadratic term, clamping at rest instead of flipping the sign.
math::Vector3 GrassDrag(const math::Vector3& velocity, float dt,
                        const Pitch& pitch) {
  math::Vector3 v = velocity;
  const float hs =
      std::sqrt(v.coords[0] * v.coords[0] + v.coords[1] * v.coords[1]);
  if (hs > 0.0f) {
    const float resistance = pitch.ground_deceleration() +
                             pitch.quadratic_resistance() * hs * hs;
    const float scale = std::max(0.0f, hs - resistance * dt) / hs;
    v.coords[0] *= scale;
    v.coords[1] *= scale;
  }
  return v;
}

// Ground resistance applies only while the ball rests on the grass and is not
// climbing; a descended interval keeps the velocity it has.
math::Vector3 LaunchVelocity(const BallModel& model, const BallState& state,
                             float dt, const Pitch& pitch) {
  if (state.position.coords[2] <= model.radius + kTouchEpsilon &&
      state.velocity.coords[2] >= -kTouchEpsilon)
    return GrassDrag(state.velocity, dt, pitch);
  return state.velocity;
}

math::Vector3 Acceleration(const BallModel& model, const math::Vector3& v0,
                           const math::Vector3& omega, const Pitch& pitch) {
  const float speed = v0.GetLength();
  math::Vector3 accel{0.0f, 0.0f, pitch.gravity()};
  if (speed > 0.0f)
    accel = accel - v0 * (pitch.air_drag() * speed);
  if (model.magnus_coefficient != 0.0f)
    accel = accel + omega.GetCrossProduct(v0) * model.magnus_coefficient;
  return accel;
}

// A ball at rest on the grass has nothing to integrate. This must require the
// rolling branch: a ball at the apex of a throw also has zero vertical speed,
// and a spinning ball still needs its orientation advanced.
bool IsAtRest(const BallState& state) {
  return state.velocity.GetSquaredLength() <= kTouchEpsilon * kTouchEpsilon &&
         state.angular_velocity.GetSquaredLength() <= kTouchEpsilon * kTouchEpsilon;
}

// One interval of contact-free motion: ballistic flight in the air, rolling on
// the grass while resting on it.
BallState IntegrateMotion(const BallModel& model, const BallState& state,
                          float dt, const Pitch& pitch) {
  if (dt <= 0.0f) return state;
  const bool rolling = IsRolling(model, state);
  if (rolling && IsAtRest(state)) return state;
  BallState next = state;
  if (rolling) {
    next.velocity = GrassDrag(state.velocity, dt, pitch);
    next.velocity.coords[2] = 0.0f;
    next.position = state.position + next.velocity * dt;
    next.position.coords[2] = model.radius;
  } else {
    const math::Vector3 v0 = LaunchVelocity(model, state, dt, pitch);
    const math::Vector3 accel = Acceleration(model, v0, state.angular_velocity, pitch);
    next.position = state.position + (v0 + accel * (0.5f * dt)) * dt;
    next.velocity = v0 + accel * dt;
  }
  next.orientation = IntegrateOrientation(state.orientation, next.angular_velocity, dt);
  return next;
}

// ---- Contact geometry ----

}  // namespace

Ball::Ball(const BallModel& model)
    : model_(model), state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {
  Info(Channel::ball,
       std::format("ball created: mass={} radius={} magnus={}", model_.mass,
                   model_.radius, model_.magnus_coefficient));
}

void Ball::Step(const Pitch& pitch) {
  constexpr float dt = time::kTickSeconds;
  const float ball_radius = model_.radius;
  const BallState start = state_;

  // 1. Predict the tick with no collisions; the impact path below only corrects
  //    this position, keeping the predicted velocity and spin.
  BallState predicted = IntegrateMotion(model_, start, dt, pitch);

  // 2. Sweep that path. A rolling ball rests on the grass, and the rolling
  //    integration already pins its centre to the radius, so the ground is not a
  //    candidate contact: query only the goal furniture.
  const auto colliders = pitch.colliders();
  const auto swept = IsRolling(model_, start) ? colliders.subspan(1) : colliders;
  const collision::MovingSphere sphere{
      {start.position, ball_radius},
      (predicted.position - start.position) * (1.0f / dt)};
  const auto found = collision::FirstHit(sphere, swept, dt);
  if (!found) {
    set_state(predicted);
    return;
  }

  const auto& hit = found->hit;
  const geometry::Collider& collider = *found->collider;

  // The single impact: stop at the contact point and change the velocity. The
  // rest of the tick is discarded.
  predicted.position = hit.center;
  const math::Vector3 contact_point = predicted.position - hit.normal * ball_radius;
  // Commit before solving: the solver reads position and velocity off the ball,
  // and the lever arm is only -normal * radius once the centre sits on the
  // surface.
  set_state(predicted);

  const auto impulse = SolveContactImpulse(
      hit.normal, contact_point, hit.surface_velocity, collider.material);
  if (impulse) ApplyImpulseAtPoint(*impulse, contact_point);
  Debug(Channel::ball, std::format("ball contact: name={}, toi={}", collider.name, hit.toi));
}

void Ball::ApplyImpulseAtPoint(const math::Vector3& impulse,
                               const math::Vector3& world_point) {
  // Something can only push the ball where it touches the shell, so the point
  // must sit one radius from the centre; anything else is a caller bug.
  const math::Vector3 r = world_point - state_.position;
  assert(std::fabs(r.GetLength() - model_.radius) <=
         kSurfaceTolerance * model_.radius);

  BallState next = state_;
  next.velocity = next.velocity + impulse * (1.0f / model_.mass);

  const math::Vector3 torque(r.GetCrossProduct(impulse));
  next.angular_velocity = next.angular_velocity + torque * (1.0f / Inertia(model_));
  set_state(next);
}

std::optional<math::Vector3> Ball::SolveContactImpulse(
    const math::Vector3& normal, const math::Vector3& contact_point,
    const math::Vector3& surface_velocity, const ContactMaterial& material) const {
  const math::Vector3 n = normal.GetNormalized({0, 0, 1});
  const math::Vector3 r = contact_point - state_.position;

  const math::Vector3& omega = state_.angular_velocity;
  const math::Vector3 omega_cross_r = omega.GetCrossProduct(r);

  const math::Vector3 u = state_.velocity + omega_cross_r - surface_velocity;
  const float un = u.GetDotProduct(n);
  if (un >= 0.0f) return std::nullopt;

  const float mass = model_.mass;
  const float jn = -mass * (1.0f + material.restitution) * un;

  // Sphere surface contacts have a radial lever arm, perpendicular to slip.
  const math::Vector3 ut = u - n * un;
  const float inverse = 1.0f / mass + r.GetDotProduct(r) / Inertia(model_);
  math::Vector3 jt = ut * (-1.0f / inverse);

  const float jt_len = jt.GetLength();
  const float mu_jn = material.friction * jn;
  if (jt_len > mu_jn) jt = jt * (mu_jn / jt_len);

  return n * jn + jt;
}

void Ball::PlaceOnGround(float x, float y) {
  BallState state;
  state.position = math::Vector3(x, y, model_.radius);
  set_state(state);
  Info(Channel::ball, std::format("placed on ground at ({}, {})", x, y));
}

}  // namespace football::ball
