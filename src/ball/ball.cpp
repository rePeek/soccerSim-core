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

// ---- Public interface ----

Ball::Ball(const BallModel& model)
    : model_(model), state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {
  Info(Channel::ball,
       std::format("ball created: mass={} radius={} magnus={}", model_.mass,
                   model_.radius, model_.magnus_coefficient));
}

BallStepResult Ball::Step(const Pitch& pitch) {
  auto result = AdvanceBallTick(pitch.colliders(), time::kTickSeconds, pitch);
  if (result.contact) {
    Debug(Channel::ball, std::format("tick resolved contact with {}; position=({}, {}, {})",
                                     result.contact->collider, state_.position.coords[0],
                                     state_.position.coords[1], state_.position.coords[2]));
  }
  return result;
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

BallStepResult Ball::AdvanceBallTick(std::span<const ColliderMotion> colliders,
                                    float dt, const Pitch& pitch) {
  BallStepResult result;
  result.state = state_;
  if (dt <= 0.0f) return result;

  constexpr int kMaxSegments = 32;
  float elapsed = 0.0f;
  std::vector<ColliderMotion> interval;
  for (int step = 0; step < kMaxSegments && elapsed < dt; ++step) {
    float duration = dt - elapsed;
    const auto& v = state_.velocity;
    const auto& w = state_.angular_velocity;
    const float az = pitch.gravity() -
        pitch.air_drag() * v.GetLength() * v.coords[2] +
        model_.magnus_coefficient *
            (w.coords[0] * v.coords[1] - w.coords[1] * v.coords[0]);
    if (v.coords[2] > 1e-4f && az < 0.0f)
      duration = std::min(duration, -v.coords[2] / az);

    std::span<const ColliderMotion> segment_colliders = colliders;
    if (elapsed != 0.0f || duration != dt) {
      interval.assign(colliders.begin(), colliders.end());
      for (std::size_t i = 0; i < colliders.size(); ++i) {
        const auto pose_at = [&](float fraction) {
          return std::visit([&](const auto& start)
              -> geometry::ColliderShape {
            using T = std::decay_t<decltype(start)>;
            const auto* end = std::get_if<T>(&colliders[i].end);
            if (!end) return start;
            if constexpr (std::is_same_v<T, Sphere>) {
              return Sphere{start.center + (end->center - start.center) * fraction,
                            start.radius + (end->radius - start.radius) * fraction};
            } else if constexpr (std::is_same_v<T, Capsule>) {
              return Capsule{start.a + (end->a - start.a) * fraction,
                             start.b + (end->b - start.b) * fraction,
                             start.radius + (end->radius - start.radius) * fraction};
            } else {
              return start;
            }
          }, colliders[i].start);
        };
        interval[i].start = pose_at(elapsed / dt);
        interval[i].end = pose_at((elapsed + duration) / dt);
      }
      segment_colliders = interval;
    }
    auto contact = AdvanceBallSegment(segment_colliders, duration, pitch);
    if (!contact) {
      elapsed += duration;
      continue;
    }
    elapsed += duration * contact->toi;
    contact->toi = elapsed / dt;
    result.contact = contact;
    set_state(AdvanceFreeFlight(state_, dt - elapsed, pitch));
    break;
  }
  result.state = state_;
  return result;
}

void Ball::PlaceOnGround(float x, float y) {
  BallState state;
  state.position = math::Vector3(x, y, model_.radius);
  set_state(state);
  Info(Channel::ball, std::format("placed on ground at ({}, {})", x, y));
}

// ---- Private implementation ----

// ---- Free-flight kinematics ----

bool Ball::OnGround(const BallState& state) const {
  return state.position.coords[2] <= model_.radius + kTouchEpsilon;
}

bool Ball::VerticallyResting(const BallState& state) {
  return std::fabs(state.velocity.coords[2]) <= kTouchEpsilon;
}

math::Vector3 Ball::GrassDrag(const math::Vector3& velocity, float span,
                             const Pitch& pitch) const {
  math::Vector3 v = velocity;
  const float hs =
      std::sqrt(v.coords[0] * v.coords[0] + v.coords[1] * v.coords[1]);
  if (hs > 0.0f) {
    const float resistance = pitch.ground_deceleration() +
                             pitch.quadratic_resistance() * hs * hs;
    const float scale = std::max(0.0f, hs - resistance * span) / hs;
    v.coords[0] *= scale;
    v.coords[1] *= scale;
  }
  return v;
}

math::Vector3 Ball::LaunchVelocity(const BallState& state, float span,
                                  const Pitch& pitch) const {
  if (OnGround(state) && state.velocity.coords[2] >= -kTouchEpsilon)
    return GrassDrag(state.velocity, span, pitch);
  return state.velocity;
}

math::Vector3 Ball::Acceleration(const math::Vector3& v0,
                                const math::Vector3& omega,
                                const Pitch& pitch) const {
  const float speed = v0.GetLength();
  math::Vector3 accel{0.0f, 0.0f, pitch.gravity()};
  if (speed > 0.0f)
    accel = accel - v0 * (pitch.air_drag() * speed);
  if (model_.magnus_coefficient != 0.0f) {
    accel = accel + math::Vector3(
                        omega.coords[1] * v0.coords[2] - omega.coords[2] * v0.coords[1],
                        omega.coords[2] * v0.coords[0] - omega.coords[0] * v0.coords[2],
                        omega.coords[0] * v0.coords[1] - omega.coords[1] * v0.coords[0]) *
                        model_.magnus_coefficient;
  }
  return accel;
}

math::Quaternion Ball::IntegrateOrientation(const math::Quaternion& q,
                                            const math::Vector3& omega, float dt) {
  const math::Quaternion omega_q(omega.coords[0], omega.coords[1], omega.coords[2], 0.0f);
  return (q + omega_q * q * (0.5f * dt)).GetNormalized();
}

BallState Ball::AdvanceFreeFlight(const BallState& state, float span,
                                  const Pitch& pitch) const {
  if (span <= 0.0f) return state;
  BallState next = state;
  if (OnGround(state) && VerticallyResting(state)) {
    next.velocity = GrassDrag(state.velocity, span, pitch);
    next.velocity.coords[2] = 0.0f;
    next.position = state.position + next.velocity * span;
    next.position.coords[2] = model_.radius;
  } else {
    const math::Vector3 v0 = LaunchVelocity(state, span, pitch);
    const math::Vector3 accel = Acceleration(v0, state.angular_velocity, pitch);
    next.position = state.position + (v0 + accel * (0.5f * span)) * span;
    next.velocity = v0 + accel * span;
  }
  next.orientation = IntegrateOrientation(state.orientation, next.angular_velocity, span);
  return next;
}

// ---- Contact resolution for one interval ----

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

std::optional<BallContact> Ball::AdvanceBallSegment(
    std::span<const ColliderMotion> colliders, float dt, const Pitch& pitch) {
  using collision::FirstHit;
  using collision::MovingSphere;

  const BallState initial = state_;
  const float eps = kTouchEpsilon;
  const float ball_radius = model_.radius;
  std::vector<geometry::ColliderId> tangent_contacts;
  const auto sweep_from = [&](const BallState& state) {
    // A repaired, separating tangent must not hide another collider's hit.
    if (tangent_contacts.empty()) {
      return FirstHit(MovingSphere{{state.position, ball_radius}, state.velocity},
                      colliders, dt);
    }
    std::optional<collision::CollisionHit> best;
    for (const auto& collider : colliders) {
      const auto hit = FirstHit(
          MovingSphere{{state.position, ball_radius}, state.velocity},
          std::span<const ColliderMotion>(&collider, 1), dt);
      if (!hit) continue;
      if (hit->toi <= eps &&
          std::find(tangent_contacts.begin(), tangent_contacts.end(), collider.id) !=
              tangent_contacts.end() &&
          (state.velocity - hit->surface_velocity).GetDotProduct(hit->normal) >= 0.0f)
        continue;
      if (!best || hit->toi < best->toi ||
          (hit->toi == best->toi && hit->collider < best->collider)) best = hit;
    }
    return best;
  };

  const auto depenetrate = [&](BallState& s, const BallContact& contact) {
    if (contact.toi > eps) return;
    for (const ColliderMotion& c : colliders) {
      if (c.id != contact.collider) continue;
      const math::Vector3 surface = std::visit(
          [&](const auto& shape) -> math::Vector3 {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Plane> ||
                          std::is_same_v<T, HalfSpace>) {
              return s.position -
                     contact.normal *
                         (s.position - shape.point).GetDotProduct(shape.normal);
            } else if constexpr (std::is_same_v<T, Sphere>) {
              const auto* end = std::get_if<Sphere>(&c.end);
              const auto center = end ? shape.center + (end->center-shape.center)*contact.toi : shape.center;
              const float radius = end ? shape.radius + (end->radius-shape.radius)*contact.toi : shape.radius;
              return center + contact.normal * radius;
            } else {
              const auto* end = std::get_if<Capsule>(&c.end);
              const auto a = end ? shape.a + (end->a-shape.a)*contact.toi : shape.a;
              const auto b = end ? shape.b + (end->b-shape.b)*contact.toi : shape.b;
              const float radius = end ? shape.radius + (end->radius-shape.radius)*contact.toi : shape.radius;
              const auto closest = collision::ClosestPointOnSegment(s.position-a, {0,0,0}, b-a);
              return a + closest + contact.normal * radius;
            }
          },
          c.start);
      s.position = surface + contact.normal * ball_radius;
      break;
    }
  };

  const auto resolve = [&](BallContact contact) -> std::optional<BallContact> {
    const ContactMaterial* material = nullptr;
    for (const ColliderMotion& c : colliders) {
      if (c.id == contact.collider) { material = &c.material; break; }
    }
    const ContactMaterial m = material ? *material : ContactMaterial{0.5f, 0.0f};
    BallState next = state_;
    const auto original_center = next.position;
    depenetrate(next, contact);
    contact.position_corrected = next.position != original_center;
    contact.point = next.position - contact.normal * ball_radius;
    set_state(next);
    const ContactImpulse solved = SolveContactImpulse(contact, m);
    if (solved.normal_impulse <= 0.0f) return std::nullopt;
    contact.normal_impulse = solved.normal_impulse;
    ApplyImpulseAtPoint(solved.impulse, contact.point);
    return contact;
  };

  const auto correction_only = [&](const BallContact& contact) {
    return contact.toi <= eps &&
           contact.relative_velocity.GetDotProduct(contact.normal) >= 0.0f;
  };
  const auto project_out = [&](BallState& s, const BallContact& contact) {
    depenetrate(s, contact);
    tangent_contacts.push_back(contact.collider);
  };
  const std::size_t correction_cap = colliders.size() + 2;

  if (OnGround(initial) && VerticallyResting(initial)) {
    BallState cursor = initial;
    cursor.position.coords[2] = ball_radius;
    cursor.velocity.coords[2] = 0.0f;
    const math::Vector3 roll_velocity = GrassDrag(cursor.velocity, dt, pitch);
    for (std::size_t guard = 0;; ++guard) {
      BallState probe = cursor;
      probe.velocity = roll_velocity;
      const auto hit = sweep_from(probe);
      if (!hit.has_value()) {
        set_state(AdvanceFreeFlight(cursor, dt, pitch));
        return std::nullopt;
      }
      auto impact_velocity = GrassDrag(initial.velocity, hit->toi * dt, pitch);
      impact_velocity.coords[2] = 0.0f;
      const BallContact first = ToBallContact(*hit, impact_velocity);
      if (!correction_only(first) || guard >= correction_cap) {
        BallState next = cursor;
        next.position = hit->center;
        next.velocity = impact_velocity;
        next.orientation = IntegrateOrientation(
            cursor.orientation, cursor.angular_velocity, first.toi * dt);
        set_state(next);
        return resolve(first);
      }
      project_out(cursor, first);
    }
  }

  BallState cursor = initial;
  for (std::size_t guard = 0;; ++guard) {
    const math::Vector3 v0 = LaunchVelocity(cursor, dt, pitch);
    const math::Vector3 accel = Acceleration(v0, cursor.angular_velocity, pitch);
    BallState probe = cursor;
    probe.velocity = v0 + accel * (0.5f * dt);
    const auto hit = sweep_from(probe);
    if (!hit.has_value()) {
      set_state(AdvanceFreeFlight(cursor, dt, pitch));
      return std::nullopt;
    }
    const math::Vector3 vc = v0 + accel * (hit->toi * dt);
    BallContact first = ToBallContact(*hit, vc);
    if (!correction_only(first) || guard >= correction_cap) {
      BallState next = cursor;
      next.position = hit->center;
      next.velocity = vc;
      next.orientation = IntegrateOrientation(cursor.orientation,
          cursor.angular_velocity, first.toi * dt);
      set_state(next);
      return resolve(first);
    }
    project_out(cursor, first);
  }
}

}  // namespace football::ball
