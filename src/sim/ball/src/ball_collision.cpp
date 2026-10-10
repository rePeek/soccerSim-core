#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_response.hpp"
#include "model/ball_config.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

#include "foundation/math/scalar.hpp"

namespace football::ball {
namespace {

constexpr float kEpsilon = 1e-6f;

// Returns the number of roots; if two, t0 <= t1.
int SolveQuadratic(float a, float b, float c, float& t0, float& t1) {
  if (std::fabs(a) < kEpsilon) {
    if (std::fabs(b) < kEpsilon) return 0;
    t0 = t1 = -c / b;
    return 1;
  }
  const float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) return 0;
  const float sq = std::sqrt(disc);
  t0 = (-b - sq) / (2.0f * a);
  t1 = (-b + sq) / (2.0f * a);
  if (t0 > t1) std::swap(t0, t1);
  return 2;
}

blunted::Vector3 ClosestPointOnSegment(const blunted::Vector3& p,
                                       const blunted::Vector3& a,
                                       const blunted::Vector3& b) {
  const blunted::Vector3 ab = b - a;
  const float len2 = ab.GetDotProduct(ab);
  if (len2 < kEpsilon) return a;
  float t = (p - a).GetDotProduct(ab) / len2;
  t = blunted::clamp(t, 0.0f, 1.0f);
  return a + ab * t;
}

blunted::Vector3 NormalizedSafe(const blunted::Vector3& v, const blunted::Vector3& fallback) {
  return v.GetNormalized(fallback);
}



std::optional<BallContact> SweepPlane(const BallState& ball, float ball_radius,
                                      const ColliderMotion& collider, const Plane& plane,
                                      float dt) {
  const blunted::Vector3 n = NormalizedSafe(plane.normal, {0, 0, 1});
  const blunted::Vector3 r0 = ball.position - plane.point;
  const blunted::Vector3 disp = ball.velocity * dt;
  const float d0 = r0.GetDotProduct(n) - ball_radius;
  const float dn = disp.GetDotProduct(n);
  BallContact contact;
  contact.collider = collider.id;
  contact.normal = n;
  contact.relative_velocity = ball.velocity;
  if (d0 < -kEpsilon) {
    contact.toi = 0.0f;
    contact.point = ball.position;
    return contact;
  }
  if (dn >= 0.0f) return std::nullopt;
  const float t = -d0 / dn;
  if (t < 0.0f || t > 1.0f) return std::nullopt;
  contact.toi = t;
  contact.point = ball.position + disp * t;
  return contact;
}

std::optional<BallContact> SweepSphere(const BallState& ball, float ball_radius,
                                       const ColliderMotion& collider, const Sphere& start,
                                       const Sphere& end, float dt) {
  const blunted::Vector3 disp = ball.velocity * dt;
  const blunted::Vector3 col_disp = end.center - start.center;
  const blunted::Vector3 r0 = ball.position - start.center;
  const blunted::Vector3 rv = disp - col_disp;
  const float radius_sum = ball_radius + start.radius;

  const float dist0 = r0.GetLength();
  BallContact contact;
  contact.collider = collider.id;
  contact.relative_velocity = ball.velocity - col_disp * (1.0f / dt);

  if (dist0 < radius_sum - kEpsilon) {
    contact.toi = 0.0f;
    contact.point = ball.position;
    contact.normal = NormalizedSafe(r0, {0, 0, 1});
    return contact;
  }

  const float a = rv.GetDotProduct(rv);
  const float b = 2.0f * r0.GetDotProduct(rv);
  const float c = r0.GetDotProduct(r0) - radius_sum * radius_sum;
  float t0, t1;
  const int roots = SolveQuadratic(a, b, c, t0, t1);
  if (roots == 0) return std::nullopt;
  const float t = (t0 >= 0.0f) ? t0 : t1;
  if (t < 0.0f || t > 1.0f) return std::nullopt;
  const blunted::Vector3 center = start.center + col_disp * t;
  const blunted::Vector3 center_ball = ball.position + disp * t;
  contact.toi = t;
  contact.point = center_ball;
  contact.normal = NormalizedSafe(center_ball - center, {0, 0, 1});
  return contact;
}

std::optional<BallContact> SweepCapsule(const BallState& ball, float ball_radius,
                                        const ColliderMotion& collider, const Capsule& start,
                                        const Capsule& end, float dt) {
  const blunted::Vector3 disp = ball.velocity * dt;
  const blunted::Vector3 col_disp = end.a - start.a;
  const blunted::Vector3 r0 = ball.position - start.a;
  const blunted::Vector3 rv = disp - col_disp;
  const blunted::Vector3 seg_a{0, 0, 0};
  const blunted::Vector3 seg_b = start.b - start.a;
  const float radius_sum = ball_radius + start.radius;

  const blunted::Vector3 closest0 = ClosestPointOnSegment(r0, seg_a, seg_b);
  const float dist0 = (r0 - closest0).GetLength();

  BallContact contact;
  contact.collider = collider.id;
  contact.relative_velocity = ball.velocity - col_disp * (1.0f / dt);

  if (dist0 < radius_sum - kEpsilon) {
    contact.toi = 0.0f;
    contact.point = ball.position;
    contact.normal = NormalizedSafe(r0 - closest0, {0, 0, 1});
    return contact;
  }

  float best = std::numeric_limits<float>::max();
  for (const blunted::Vector3& center : {seg_a, seg_b}) {
    const blunted::Vector3 rr0 = r0 - center;
    const float a = rv.GetDotProduct(rv);
    const float b = 2.0f * rr0.GetDotProduct(rv);
    const float c = rr0.GetDotProduct(rr0) - radius_sum * radius_sum;
    float t0, t1;
    const int roots = SolveQuadratic(a, b, c, t0, t1);
    if (roots == 0) continue;
    const float t = (t0 >= 0.0f) ? t0 : t1;
    if (t >= 0.0f && t <= 1.0f) best = std::min(best, t);
  }

  // Interior: squared distance to the infinite line equals radius_sum^2.
  const blunted::Vector3 dir_v = seg_b - seg_a;
  const float dir_len2 = dir_v.GetDotProduct(dir_v);
  if (dir_len2 > kEpsilon) {
    const blunted::Vector3 d = dir_v * (1.0f / std::sqrt(dir_len2));
    const float ca = rv.GetDotProduct(rv) - std::pow(rv.GetDotProduct(d), 2.0f);
    const float cb = 2.0f * (r0.GetDotProduct(rv) - r0.GetDotProduct(d) * rv.GetDotProduct(d));
    const float cc = r0.GetDotProduct(r0) - std::pow(r0.GetDotProduct(d), 2.0f) - radius_sum * radius_sum;
    float t0, t1;
    const int roots = SolveQuadratic(ca, cb, cc, t0, t1);
    if (roots > 0) {
      const float t = (t0 >= 0.0f) ? t0 : t1;
      if (t >= 0.0f && t <= 1.0f) {
        const blunted::Vector3 at = r0 + rv * t;
        const float s = at.GetDotProduct(d);
        if (s >= -kEpsilon && s <= std::sqrt(dir_len2) + kEpsilon) {
          best = std::min(best, t);
        }
      }
    }
  }

  if (best > 1.0f) return std::nullopt;
  const blunted::Vector3 center_ball = ball.position + disp * best;
  const blunted::Vector3 r_at = r0 + rv * best;
  const blunted::Vector3 closest = ClosestPointOnSegment(r_at, seg_a, seg_b);
  contact.toi = best;
  contact.point = center_ball;
  contact.normal = NormalizedSafe(r_at - closest, {0, 0, 1});
  return contact;
}

}  // namespace

std::optional<BallContact> SweepBall(const BallState& ball, const ColliderMotion& collider,
                                     float dt, float ball_radius) {
  return std::visit(
      [&](const auto& start_shape) -> std::optional<BallContact> {
        using T = std::decay_t<decltype(start_shape)>;
        if constexpr (std::is_same_v<T, Plane>) {
          return SweepPlane(ball, ball_radius, collider, start_shape, dt);
        } else if constexpr (std::is_same_v<T, Sphere>) {
          const auto* end = std::get_if<Sphere>(&collider.end);
          if (end == nullptr) return std::nullopt;
          return SweepSphere(ball, ball_radius, collider, start_shape, *end, dt);
        } else if constexpr (std::is_same_v<T, Capsule>) {
          const auto* end = std::get_if<Capsule>(&collider.end);
          if (end == nullptr) return std::nullopt;
          return SweepCapsule(ball, ball_radius, collider, start_shape, *end, dt);
        }
        return std::nullopt;
      },
      collider.start);
}

std::optional<BallContact> FirstContact(const BallState& ball,
                                        std::span<const ColliderMotion> colliders,
                                        float dt, float ball_radius) {
  std::optional<BallContact> best;
  for (const ColliderMotion& collider : colliders) {
    const std::optional<BallContact> hit = SweepBall(ball, collider, dt, ball_radius);
    if (!hit.has_value()) continue;
    if (!best.has_value() ||
        hit->toi < best->toi - kEpsilon ||
        (std::fabs(hit->toi - best->toi) <= kEpsilon && hit->collider < best->collider)) {
      best = hit;
    }
  }
  return best;
}

BallStepResult AdvanceBall(const BallState& initial,
                           std::span<const ColliderMotion> colliders,
                           float dt, float ball_radius, float restitution,
                           std::size_t max_contacts) {
  BallStepResult result;
  result.state = initial;
  float remaining = dt;
  for (std::size_t i = 0; i < max_contacts && remaining > kEpsilon; ++i) {
    const BallState probe = result.state;
    const std::optional<BallContact> first =
        FirstContact(probe, colliders, remaining, ball_radius);
    if (!first.has_value()) {
      result.state.position = result.state.position + result.state.velocity * remaining;
      remaining = 0.0f;
      break;
    }
    BallContact contact = *first;
    result.contacts.push_back(contact);
    if (contact.toi <= kEpsilon) {
      // Resting or initially penetrating: remove normal velocity and stop to
      // avoid a TOI=0 loop.
      contact.normal = contact.normal.GetNormalized({0, 0, 1});
      const float vn = result.state.velocity.GetDotProduct(contact.normal);
      if (vn < 0.0f) {
        result.state.velocity = result.state.velocity - contact.normal * vn;
      }
      remaining = 0.0f;
      break;
    }
    // Advance to the contact instant and reflect the normal velocity.
    result.state.position = result.state.position +
        result.state.velocity * (contact.toi * remaining);
    remaining *= (1.0f - contact.toi);
    contact.normal = contact.normal.GetNormalized({0, 0, 1});
    const float vn = result.state.velocity.GetDotProduct(contact.normal);
    if (vn < 0.0f) {
      result.state.velocity = result.state.velocity -
          contact.normal * ((1.0f + restitution) * vn);
    }
  }
  return result;
}

BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const ColliderMotion> colliders,
                               float dt,
                               const football::model::BallConfig& config,
                               const BallDynamics& dynamics) {
  BallStepResult result;
  result.state = initial;
  const float eps = 1e-4f;
  const float ball_radius = config.radius();

  const auto integrate_orientation = [](const blunted::Quaternion& q,
                                        const blunted::Vector3& omega, float dt) {
    const blunted::Quaternion omega_q(omega.coords[0], omega.coords[1], omega.coords[2], 0.0f);
    return (q + omega_q * q * (0.5f * dt)).GetNormalized();
  };

  const auto collider_velocity_of = [&](ColliderId id) -> blunted::Vector3 {
    for (const ColliderMotion& c : colliders) {
      if (c.id != id) continue;
      return std::visit([&](const auto& s) -> blunted::Vector3 {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Plane>) {
          return {0, 0, 0};
        } else if constexpr (std::is_same_v<T, Sphere>) {
          const auto* e = std::get_if<Sphere>(&c.end);
          return e ? (e->center - s.center) * (1.0f / dt) : blunted::Vector3{0, 0, 0};
        } else if constexpr (std::is_same_v<T, Capsule>) {
          const auto* e = std::get_if<Capsule>(&c.end);
          return e ? (e->a - s.a) * (1.0f / dt) : blunted::Vector3{0, 0, 0};
        }
        return {0, 0, 0};
      }, c.start);
    }
    return {0, 0, 0};
  };

  // Resting contact that starts already penetrating must be projected back to
  // the surface before the impulse is applied; otherwise a ball below grass
  // level would keep bouncing in place without ever being separated.
  const auto depenetrate = [&](BallState& s, const BallContact& contact) {
    if (contact.toi > eps) return;
    for (const ColliderMotion& c : colliders) {
      if (c.id != contact.collider) continue;
      const blunted::Vector3 surface = std::visit(
          [&](const auto& shape) -> blunted::Vector3 {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Plane>) {
              return s.position -
                     contact.normal *
                         (s.position - shape.point).GetDotProduct(shape.normal);
            } else if constexpr (std::is_same_v<T, Sphere>) {
              return shape.center + contact.normal * shape.radius;
            } else {
              const blunted::Vector3 closest = ClosestPointOnSegment(
                  s.position - shape.a, {0, 0, 0}, shape.b - shape.a);
              return shape.a + closest + contact.normal * shape.radius;
            }
          },
          c.start);
      s.position = surface + contact.normal * ball_radius;
      break;
    }
  };

  const auto resolve = [&](const BallContact& contact) {
    const ContactMaterial* material = nullptr;
    for (const ColliderMotion& c : colliders) {
      if (c.id == contact.collider) { material = &c.material; break; }
    }
    const ContactMaterial m = material ? *material : ContactMaterial{0.5f, 0.0f};
    depenetrate(result.state, contact);
    result.state = ResolveContact(result.state, contact, m,
                                  collider_velocity_of(contact.collider), config);
    result.contacts.push_back(contact);
  };

  // Grass resistance applies in a thin zone above the ground; grass height
  // only widens the resistance zone, the support height is exactly the radius.
  const float resting_z = ball_radius;
  const float grass_zone = resting_z + dynamics.grass_height;
  const bool on_ground = initial.position.coords[2] <= resting_z + eps;
  const bool in_grass_zone = initial.position.coords[2] <= grass_zone + eps;
  const bool not_descending = initial.velocity.coords[2] >= -eps;

  blunted::Vector3 grass_v = initial.velocity;
  if (in_grass_zone && not_descending) {
    const float hs = std::sqrt(grass_v.coords[0] * grass_v.coords[0] +
                               grass_v.coords[1] * grass_v.coords[1]);
    if (hs > 0.0f) {
      const float resistance = dynamics.ground_deceleration +
                               dynamics.quadratic_ground_resistance * hs * hs;
      const float slowed = std::max(0.0f, hs - resistance * dt);
      const float scale = slowed / hs;
      grass_v.coords[0] *= scale;
      grass_v.coords[1] *= scale;
    }
  }

  const float spin_scale = std::max(0.0f, 1.0f - dynamics.spin_decay * dt);
  const blunted::Vector3 omega1 = initial.angular_velocity * spin_scale;

  // Persistent ground contact: rolling constraint, but still check other
  // (non-ground) colliders; the tangent ground plane produces no ground hit.
  if (on_ground && not_descending) {
    blunted::Vector3 v = grass_v;
    v.coords[2] = 0.0f;
    BallState probe = initial;
    probe.velocity = v;
    probe.position.coords[2] = resting_z;
    probe.angular_velocity = omega1;
    const std::optional<BallContact> hit = FirstContact(probe, colliders, dt, ball_radius);
    if (hit.has_value()) {
      result.state = probe;
      result.state.position = hit->point;
      result.state.velocity = v;
      resolve(*hit);
    } else {
      result.state.velocity = v;
      result.state.position = initial.position + v * dt;
      result.state.position.coords[2] = resting_z;
      result.state.angular_velocity = omega1;
      result.state.orientation = integrate_orientation(initial.orientation, omega1, dt);
    }
    return result;
  }

  // Airborne free-motion candidate.
  const blunted::Vector3 gravity(0, 0, -dynamics.gravity);
  const blunted::Vector3 v0 = (in_grass_zone && not_descending) ? grass_v : initial.velocity;
  const float speed = v0.GetLength();
  blunted::Vector3 accel = gravity;
  if (speed > 0.0f) accel = accel - v0 * (dynamics.quadratic_resistance * speed);
  if (dynamics.magnus_coefficient != 0.0f) {
    const blunted::Vector3& o = initial.angular_velocity;
    accel = accel + blunted::Vector3(
        o.coords[1] * v0.coords[2] - o.coords[2] * v0.coords[1],
        o.coords[2] * v0.coords[0] - o.coords[0] * v0.coords[2],
        o.coords[0] * v0.coords[1] - o.coords[1] * v0.coords[0]) *
            dynamics.magnus_coefficient;
  }
  const blunted::Vector3 v1 = v0 + accel * dt;
  const blunted::Vector3 vavg = v0 + accel * (0.5f * dt);
  const blunted::Vector3 p1 = initial.position + vavg * dt;

  BallState probe = initial;
  probe.velocity = vavg;
  const std::optional<BallContact> first = FirstContact(probe, colliders, dt, ball_radius);
  if (!first.has_value()) {
    result.state.position = p1;
    result.state.velocity = v1;
    result.state.angular_velocity = omega1;
    result.state.orientation = integrate_orientation(initial.orientation, omega1, dt);
    return result;
  }

  // Move to the contact instant and let ResolveContact also spin the ball.
  const blunted::Vector3 vc = v0 + accel * (*first).toi * dt;
  result.state.position = (*first).point;
  result.state.velocity = vc;
  result.state.angular_velocity = omega1;
  result.state.orientation =
      integrate_orientation(initial.orientation, initial.angular_velocity, (*first).toi * dt);
  resolve(*first);
  return result;
}

}  // namespace football::ball