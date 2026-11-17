#include "football/ball/ball.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/log/log.hpp"
#include "football/pitch/pitch.hpp"

using namespace football::log;
using namespace football::math;

namespace football::ball {

using geometry::Capsule;
using geometry::ContactMaterial;
using geometry::HalfSpace;
using geometry::Plane;
using geometry::Sphere;

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

// The ball rolls while it rests on the grass, and flies otherwise. This is the
// single definition of "on the ground and neither climbing nor falling", and it
// picks the integrator's branch.
bool IsRolling(const BallModel& model, const BallState& state) {
  return state.position.coords[2] <= model.radius + kTouchEpsilon &&
         std::fabs(state.velocity.coords[2]) <= kTouchEpsilon;
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

// First-order quaternion integration of a spin over dt, renormalized because the
// Euler step does not preserve the unit length.
math::Quaternion IntegrateOrientation(const math::Quaternion& q,
                                      const math::Vector3& omega, float dt) {
  const math::Quaternion omega_q(omega.coords[0], omega.coords[1], omega.coords[2], 0.0f);
  return (q + omega_q * q * (0.5f * dt)).GetNormalized();
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

BallContact ToBallContact(const collision::CollisionHit& hit,
                          const math::Vector3& velocity, float ball_radius) {
  BallContact contact;
  contact.toi = hit.toi;
  contact.point = hit.center - hit.normal * ball_radius;
  contact.normal = hit.normal;
  contact.surface_velocity = hit.surface_velocity;
  contact.relative_velocity = velocity - hit.surface_velocity;
  return contact;
}

// Sweeps the path from start to the predicted next state, so a fast ball cannot
// pass through a collider whose endpoints both lie outside it. Colliders already
// repaired for an initial overlap must not hide a later, real impact.
std::optional<collision::ColliderHit> FindContact(
    float ball_radius, const BallState& start, const BallState& next,
    std::span<const geometry::Collider> colliders, float dt,
    const std::vector<const geometry::Collider*>& repaired) {
  const collision::MovingSphere sphere{
      {start.position, ball_radius}, (next.position - start.position) * (1.0f / dt)};
  if (repaired.empty()) return collision::FirstHit(sphere, colliders, dt);

  std::optional<collision::ColliderHit> best;
  for (const auto& collider : colliders) {
    const auto hit = collision::Sweep(sphere, collider.shape, dt);
    if (!hit) continue;
    const bool was_repaired =
        std::find(repaired.begin(), repaired.end(), &collider) != repaired.end();
    // Judge separation on the predicted outcome, not the tick-start velocity: a
    // ball that starts on the surface with a small upward speed looks separating at
    // the start, yet its apex falls back inside this tick.
    const float normal_velocity =
        (next.velocity - hit->surface_velocity).GetDotProduct(hit->normal);
    if (was_repaired && hit->toi <= kTouchEpsilon && normal_velocity >= 0.0f)
      continue;
    if (!best || hit->toi < best->hit.toi)
      best = collision::ColliderHit{*hit, &collider};
  }
  return best;
}

// Moves the centre back onto the surface of a collider the ball overlaps. Only
// the position changes; the contact is not an impact by itself.
void Depenetrate(BallState& state, const BallContact& contact,
                 const geometry::Collider& collider, float ball_radius) {
  if (contact.toi > kTouchEpsilon) return;
  const math::Vector3 surface = std::visit(
      [&](const auto& shape) -> math::Vector3 {
        using T = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<T, Plane> || std::is_same_v<T, HalfSpace>) {
          const auto normal = shape.normal.GetNormalized({0, 0, 1});
          return state.position -
              normal * (state.position - shape.point).GetDotProduct(normal);
        } else if constexpr (std::is_same_v<T, Sphere>) {
          return shape.center + contact.normal * shape.radius;
        } else {
          const auto closest = collision::ClosestPointOnSegment(
              state.position - shape.a, {0, 0, 0}, shape.b - shape.a);
          return shape.a + closest + contact.normal * shape.radius;
        }
      },
      collider.shape);
  state.position = surface + contact.normal * ball_radius;
}

}  // namespace

// ---- Public interface ----

Ball::Ball(const BallModel& model)
    : model_(model), state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {
  Info(Channel::ball,
       std::format("ball created: mass={} radius={} magnus={}", model_.mass,
                   model_.radius, model_.magnus_coefficient));
}

void Ball::Step(const Pitch& pitch) {
  constexpr float dt = time::kTickSeconds;
  const auto colliders = pitch.colliders();
  const float ball_radius = model_.radius;
  BallState start = state_;
  std::vector<const geometry::Collider*> repaired;
  const std::size_t repair_cap = colliders.size() + 2;
  for (std::size_t guard = 0;; ++guard) {
    // 1. Predict the entire tick without collisions. Repairs below only adjust
    // an initially overlapping start; they do not subdivide simulation time.
    BallState next = IntegrateMotion(model_, start, dt, pitch);

    // 2. Sweep the path to that predicted position, not just its endpoint.
    // TOI locates the contact geometrically; it never scales the integration.
    const auto found = FindContact(ball_radius, start, next, colliders, dt, repaired);
    if (!found) {
      set_state(next);
      return;
    }
    const auto& hit = found->hit;
    const geometry::Collider& collider = *found->collider;

    // 3. Evaluate the ball at the moment of contact: only the geometry comes from
    // the straight sweep, position and velocity are integrated up to `toi`.
    const float contact_dt = dt * hit.toi;
    BallState at_contact = IntegrateMotion(model_, start, contact_dt, pitch);
    at_contact.position = hit.center;

    BallContact contact = ToBallContact(hit, at_contact.velocity, ball_radius);

    // Initial overlap: repair the start and sweep again. This is not an impact,
    // so it does not spend the tick's single impulse.
    if (contact.toi <= kTouchEpsilon &&
        contact.relative_velocity.GetDotProduct(contact.normal) >= 0.0f &&
        guard < repair_cap) {
      Depenetrate(start, contact, collider, ball_radius);
      repaired.push_back(found->collider);
      continue;
    }

    const auto original_center = at_contact.position;
    Depenetrate(at_contact, contact, collider, ball_radius);
    contact.position_corrected = at_contact.position != original_center;
    contact.point = at_contact.position - contact.normal * ball_radius;
    // Commit before solving: the solver reads position and velocity off the ball,
    // and the lever arm is only -normal * radius once the centre sits on the
    // surface. A separating contact leaves this state as is.
    set_state(at_contact);

    const auto impulse = SolveContactImpulse(contact, collider.material);
    if (impulse) ApplyImpulseAtPoint(*impulse, contact.point);
    Debug(Channel::ball, std::format("ball contact: name={}, toi={}", collider.name, hit.toi));
    // The rest of the tick is deliberately discarded: one impact per tick.
    return;
  }
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
    const BallContact& contact, const ContactMaterial& material) const {
  const math::Vector3 n = contact.normal.GetNormalized({0, 0, 1});
  const math::Vector3 r = contact.point - state_.position;

  const math::Vector3& omega = state_.angular_velocity;
  const math::Vector3 omega_cross_r = omega.GetCrossProduct(r);

  const math::Vector3 u = state_.velocity + omega_cross_r - contact.surface_velocity;
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
