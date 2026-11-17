#include "football/ball/ball.hpp"

#include <algorithm>
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
using geometry::ColliderMotion;
using geometry::ContactMaterial;
using geometry::HalfSpace;
using geometry::Plane;
using geometry::Sphere;

namespace {

// Touching distance to the grass and to the interval start: closer than this is
// "already in contact", not a new impact.
constexpr float kTouchEpsilon = 1e-4f;

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
  if (model.magnus_coefficient != 0.0f) {
    accel = accel + math::Vector3(
                        omega.coords[1] * v0.coords[2] - omega.coords[2] * v0.coords[1],
                        omega.coords[2] * v0.coords[0] - omega.coords[0] * v0.coords[2],
                        omega.coords[0] * v0.coords[1] - omega.coords[1] * v0.coords[0]) *
                        model.magnus_coefficient;
  }
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
  BallState start = state_;
  std::vector<geometry::ColliderId> repaired;
  const std::size_t repair_cap = colliders.size() + 2;
  for (std::size_t guard = 0;; ++guard) {
    // 1. Predict the entire tick without collisions. Repairs below only adjust
    // an initially overlapping start; they do not subdivide simulation time.
    BallState next = IntegrateMotion(model_, start, dt, pitch);

    // 2. Sweep the path to that predicted position, not just its endpoint.
    // TOI locates the contact geometrically; it never scales the integration.
    const auto hit = FindContact(start, next, colliders, dt, repaired);
    if (!hit) {
      set_state(next);
      return;
    }

    BallContact contact = ToBallContact(*hit, next.velocity);
    if (contact.toi <= kTouchEpsilon &&
        contact.relative_velocity.GetDotProduct(contact.normal) >= 0.0f &&
        guard < repair_cap) {
      Depenetrate(start, contact, colliders);
      repaired.push_back(contact.collider);
      continue;
    }

    // 3. Keep the predicted velocity, spin and orientation, but stop the centre
    // at first contact. Resolve just one passive impulse, with no tail motion.
    next.position = hit->center;
    const auto original_center = next.position;
    Depenetrate(next, contact, colliders);
    contact.position_corrected = next.position != original_center;
    contact.point = next.position - contact.normal * model_.radius;
    set_state(next);

    const ContactMaterial* material = nullptr;
    for (const auto& collider : colliders) {
      if (collider.id == contact.collider) {
        material = &collider.material;
        break;
      }
    }
    const ContactMaterial m = material ? *material : ContactMaterial{0.5f, 0.0f};
    const ContactImpulse solved = SolveContactImpulse(contact, m);
    if (!(solved.normal_impulse > 0.0f)) return;

    contact.normal_impulse = solved.normal_impulse;
    ApplyImpulseAtPoint(solved.impulse, contact.point);
    Debug(Channel::ball, std::format("tick resolved contact with {}; position=({}, {}, {})",
                                     contact.collider, state_.position.coords[0],
                                     state_.position.coords[1], state_.position.coords[2]));
    return;
  }
}

void Ball::ApplyImpulseAtPoint(const math::Vector3& impulse,
                               const math::Vector3& world_point) {
  BallState next = state_;
  next.velocity = next.velocity + impulse * (1.0f / model_.mass);

  const math::Vector3 r = world_point - state_.position;
  const math::Vector3 torque(
      r.coords[1] * impulse.coords[2] - r.coords[2] * impulse.coords[1],
      r.coords[2] * impulse.coords[0] - r.coords[0] * impulse.coords[2],
      r.coords[0] * impulse.coords[1] - r.coords[1] * impulse.coords[0]);
  next.angular_velocity = next.angular_velocity + torque * (1.0f / inertia());
  set_state(next);
}

Ball::ContactImpulse Ball::SolveContactImpulse(
    const BallContact& contact, const ContactMaterial& material) const {
  const math::Vector3 n = contact.normal.GetNormalized({0, 0, 1});
  const math::Vector3 r = contact.point - state_.position;

  const math::Vector3& omega = state_.angular_velocity;
  const math::Vector3 omega_cross_r(
      omega.coords[1] * r.coords[2] - omega.coords[2] * r.coords[1],
      omega.coords[2] * r.coords[0] - omega.coords[0] * r.coords[2],
      omega.coords[0] * r.coords[1] - omega.coords[1] * r.coords[0]);

  const math::Vector3 u = state_.velocity + omega_cross_r - contact.surface_velocity;
  const float un = u.GetDotProduct(n);
  if (un >= 0.0f) return {};

  const float mass = model_.mass;
  const float jn = -mass * (1.0f + material.restitution) * un;

  // Sphere surface contacts have a radial lever arm, perpendicular to slip.
  const math::Vector3 ut = u - n * un;
  const float inverse = 1.0f / mass + r.GetDotProduct(r) / inertia();
  math::Vector3 jt = ut * (-1.0f / inverse);

  const float jt_len = jt.GetLength();
  const float mu_jn = material.friction * jn;
  if (jt_len > mu_jn) jt = jt * (mu_jn / jt_len);

  return {n * jn + jt, jn};
}

void Ball::PlaceOnGround(float x, float y) {
  BallState state;
  state.position = math::Vector3(x, y, model_.radius);
  set_state(state);
  Info(Channel::ball, std::format("placed on ground at ({}, {})", x, y));
}

// ---- Passive contact geometry ----

BallContact Ball::ToBallContact(const collision::CollisionHit& hit,
                                const math::Vector3& velocity) const {
  BallContact contact;
  contact.collider = hit.collider;
  contact.toi = hit.toi;
  contact.point = hit.center - hit.normal * model_.radius;
  contact.normal = hit.normal;
  contact.surface_velocity = hit.surface_velocity;
  contact.relative_velocity = velocity - hit.surface_velocity;
  return contact;
}

std::optional<collision::CollisionHit> Ball::FindContact(
    const BallState& start, const BallState& next,
    std::span<const ColliderMotion> colliders, float dt,
    std::span<const geometry::ColliderId> repaired) const {
  const collision::MovingSphere sphere{
      {start.position, model_.radius}, (next.position - start.position) * (1.0f / dt)};
  if (repaired.empty()) return collision::FirstHit(sphere, colliders, dt);

  // An initial, separating overlap must not hide another collider's impact.
  std::optional<collision::CollisionHit> best;
  for (const auto& collider : colliders) {
    const auto hit = collision::FirstHit(
        sphere, std::span<const ColliderMotion>(&collider, 1), dt);
    if (!hit) continue;
    if (hit->toi <= kTouchEpsilon &&
        std::find(repaired.begin(), repaired.end(), collider.id) != repaired.end() &&
        (next.velocity - hit->surface_velocity).GetDotProduct(hit->normal) >= 0.0f)
      continue;
    if (!best || hit->toi < best->toi ||
        (hit->toi == best->toi && hit->collider < best->collider)) best = hit;
  }
  return best;
}

void Ball::Depenetrate(BallState& state, const BallContact& contact,
                       std::span<const ColliderMotion> colliders) const {
  if (contact.toi > kTouchEpsilon) return;
  for (const auto& collider : colliders) {
    if (collider.id != contact.collider) continue;
    const math::Vector3 surface = std::visit(
        [&](const auto& shape) -> math::Vector3 {
          using T = std::decay_t<decltype(shape)>;
          if constexpr (std::is_same_v<T, Plane> || std::is_same_v<T, HalfSpace>) {
            const auto normal = shape.normal.GetNormalized({0, 0, 1});
            return state.position - normal *
                (state.position - shape.point).GetDotProduct(normal);
          } else if constexpr (std::is_same_v<T, Sphere>) {
            const auto& end = std::get<Sphere>(collider.end);
            const auto center = shape.center + (end.center - shape.center) * contact.toi;
            const float radius = shape.radius + (end.radius - shape.radius) * contact.toi;
            return center + contact.normal * radius;
          } else {
            const auto& end = std::get<Capsule>(collider.end);
            const auto a = shape.a + (end.a - shape.a) * contact.toi;
            const auto b = shape.b + (end.b - shape.b) * contact.toi;
            const float radius = shape.radius + (end.radius - shape.radius) * contact.toi;
            const auto closest = collision::ClosestPointOnSegment(state.position - a, {0, 0, 0}, b - a);
            return a + closest + contact.normal * radius;
          }
        }, collider.start);
    state.position = surface + contact.normal * model_.radius;
    break;
  }
}

}  // namespace football::ball
