#include "football/foundation/collision/sweep.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

#include "football/foundation/math/scalar.hpp"
#include "sweep_deforming_capsule.hpp"

namespace football::collision {

using football::geometry::Capsule;
using football::geometry::ColliderMotion;
using football::geometry::Plane;
using football::geometry::Sphere;

football::math::Vector3 ClosestPointOnSegment(const football::math::Vector3& p,
                                       const football::math::Vector3& a,
                                       const football::math::Vector3& b) {
  const football::math::Vector3 ab = b - a;
  const float len2 = ab.GetDotProduct(ab);
  if (len2 == 0.0f) return a;
  float t = (p - a).GetDotProduct(ab) / len2;
  t = football::math::clamp(t, 0.0f, 1.0f);
  return a + ab * t;
}

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

football::math::Vector3 NormalizedSafe(const football::math::Vector3& v,
                                const football::math::Vector3& fallback) {
  return v.GetNormalized(fallback);
}

std::optional<CollisionHit> SweepPlane(const MovingSphere& sphere,
                                       const ColliderMotion& collider,
                                       const Plane& plane, float dt) {
  const football::math::Vector3 ball_position = sphere.shape.center;
  const football::math::Vector3 ball_velocity = sphere.velocity;
  const float ball_radius = sphere.shape.radius;

  const football::math::Vector3 n = NormalizedSafe(plane.normal, {0, 0, 1});
  const football::math::Vector3 r0 = ball_position - plane.point;
  const football::math::Vector3 disp = ball_velocity * dt;
  const float d0 = r0.GetDotProduct(n) - ball_radius;
  const float dn = disp.GetDotProduct(n);
  CollisionHit hit;
  hit.collider = collider.id;
  hit.normal = n;
  if (d0 < -kEpsilon) {
    hit.toi = 0.0f;
    hit.center = ball_position;
    return hit;
  }
  if (dn >= 0.0f) return std::nullopt;
  const float t = -d0 / dn;
  if (t < 0.0f || t > 1.0f) return std::nullopt;
  hit.toi = t;
  hit.center = ball_position + disp * t;
  return hit;
}

std::optional<CollisionHit> SweepSphere(const MovingSphere& sphere,
                                        const ColliderMotion& collider,
                                        const Sphere& start, const Sphere& end,
                                        float dt) {
  const football::math::Vector3 ball_position = sphere.shape.center;
  const football::math::Vector3 ball_velocity = sphere.velocity;
  const float ball_radius = sphere.shape.radius;

  const football::math::Vector3 disp = ball_velocity * dt;
  const football::math::Vector3 col_disp = end.center - start.center;
  const football::math::Vector3 r0 = ball_position - start.center;
  const football::math::Vector3 rv = disp - col_disp;
  const float radius_sum = ball_radius + start.radius;

  const float dist0 = r0.GetLength();
  CollisionHit hit;
  hit.collider = collider.id;
  hit.surface_velocity = col_disp * (1.0f / dt);

  if (dist0 < radius_sum - kEpsilon) {
    hit.toi = 0.0f;
    hit.center = ball_position;
    hit.normal = NormalizedSafe(r0, {0, 0, 1});
    return hit;
  }
  // Outside/tangent and non-approaching: reject the quadratic exit root.
  // Convex distance cannot later decrease along this straight relative sweep.
  if (r0.GetDotProduct(rv) >= 0.0f) return std::nullopt;

  const float a = rv.GetDotProduct(rv);
  const float b = 2.0f * r0.GetDotProduct(rv);
  const float c = r0.GetDotProduct(r0) - radius_sum * radius_sum;
  float t0, t1;
  const int roots = SolveQuadratic(a, b, c, t0, t1);
  if (roots == 0) return std::nullopt;
  const float t = (t0 >= 0.0f) ? t0 : t1;
  if (t < 0.0f || t > 1.0f) return std::nullopt;
  const football::math::Vector3 center = start.center + col_disp * t;
  const football::math::Vector3 center_ball = ball_position + disp * t;
  hit.toi = t;
  hit.center = center_ball;
  hit.normal = NormalizedSafe(center_ball - center, {0, 0, 1});
  return hit;
}

std::optional<CollisionHit> SweepCapsule(const MovingSphere& sphere,
                                         const ColliderMotion& collider,
                                         const Capsule& start, const Capsule& end,
                                         float dt) {
  const football::math::Vector3 ball_position = sphere.shape.center;
  const football::math::Vector3 ball_velocity = sphere.velocity;
  const float ball_radius = sphere.shape.radius;

  const football::math::Vector3 disp = ball_velocity * dt;
  const football::math::Vector3 col_disp = end.a - start.a;
  const football::math::Vector3 r0 = ball_position - start.a;
  const football::math::Vector3 rv = disp - col_disp;
  const football::math::Vector3 seg_a{0, 0, 0};
  const football::math::Vector3 seg_b = start.b - start.a;
  const float radius_sum = ball_radius + start.radius;

  const football::math::Vector3 closest0 = ClosestPointOnSegment(r0, seg_a, seg_b);
  const float dist0 = (r0 - closest0).GetLength();

  CollisionHit hit;
  hit.collider = collider.id;
  hit.surface_velocity = col_disp * (1.0f / dt);

  if (dist0 < radius_sum - kEpsilon) {
    hit.toi = 0.0f;
    hit.center = ball_position;
    hit.normal = NormalizedSafe(r0 - closest0, {0, 0, 1});
    return hit;
  }
  if ((r0 - closest0).GetDotProduct(rv) >= 0.0f) return std::nullopt;

  float best = std::numeric_limits<float>::max();
  for (const football::math::Vector3& center : {seg_a, seg_b}) {
    const football::math::Vector3 rr0 = r0 - center;
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
  const football::math::Vector3 dir_v = seg_b - seg_a;
  const float dir_len2 = dir_v.GetDotProduct(dir_v);
  if (dir_len2 > kEpsilon) {
    const football::math::Vector3 d = dir_v * (1.0f / std::sqrt(dir_len2));
    const float ca = rv.GetDotProduct(rv) - std::pow(rv.GetDotProduct(d), 2.0f);
    const float cb = 2.0f * (r0.GetDotProduct(rv) - r0.GetDotProduct(d) * rv.GetDotProduct(d));
    const float cc = r0.GetDotProduct(r0) - std::pow(r0.GetDotProduct(d), 2.0f) - radius_sum * radius_sum;
    float t0, t1;
    const int roots = SolveQuadratic(ca, cb, cc, t0, t1);
    if (roots > 0) {
      const float t = (t0 >= 0.0f) ? t0 : t1;
      if (t >= 0.0f && t <= 1.0f) {
        const football::math::Vector3 at = r0 + rv * t;
        const float s = at.GetDotProduct(d);
        if (s >= -kEpsilon && s <= std::sqrt(dir_len2) + kEpsilon) {
          best = std::min(best, t);
        }
      }
    }
  }

  if (best > 1.0f) return std::nullopt;
  const football::math::Vector3 center_ball = ball_position + disp * best;
  const football::math::Vector3 r_at = r0 + rv * best;
  const football::math::Vector3 closest = ClosestPointOnSegment(r_at, seg_a, seg_b);
  hit.toi = best;
  hit.center = center_ball;
  hit.normal = NormalizedSafe(r_at - closest, {0, 0, 1});
  return hit;
}

}  // namespace

std::optional<CollisionHit> Sweep(const MovingSphere& sphere,
                                  const ColliderMotion& collider, float dt) {
  return std::visit(
      [&](const auto& start_shape) -> std::optional<CollisionHit> {
        using T = std::decay_t<decltype(start_shape)>;
        if constexpr (std::is_same_v<T, Plane>) {
          return SweepPlane(sphere, collider, start_shape, dt);
        } else if constexpr (std::is_same_v<T, Sphere>) {
          const auto* end = std::get_if<Sphere>(&collider.end);
          if (end == nullptr) return std::nullopt;
          if (start_shape.radius != end->radius)
            return detail::SweepDeformingCapsule(sphere, collider,
                Capsule{start_shape.center, start_shape.center, start_shape.radius},
                Capsule{end->center, end->center, end->radius}, dt);
          return SweepSphere(sphere, collider, start_shape, *end, dt);
        } else if constexpr (std::is_same_v<T, Capsule>) {
          const auto* end = std::get_if<Capsule>(&collider.end);
          if (end == nullptr) return std::nullopt;
          if (end->a - start_shape.a != end->b - start_shape.b || start_shape.radius != end->radius)
            return detail::SweepDeformingCapsule(sphere, collider, start_shape, *end, dt);
          return SweepCapsule(sphere, collider, start_shape, *end, dt);
        }
        return std::nullopt;
      },
      collider.start);
}

std::optional<CollisionHit> FirstHit(
    const MovingSphere& sphere,
    std::span<const ColliderMotion> colliders, float dt) {
  std::optional<CollisionHit> best;
  for (const ColliderMotion& collider : colliders) {
    const std::optional<CollisionHit> hit = Sweep(sphere, collider, dt);
    if (!hit.has_value()) continue;
    if (!best.has_value() ||
        hit->toi < best->toi - kEpsilon ||
        (std::fabs(hit->toi - best->toi) <= kEpsilon && hit->collider < best->collider)) {
      best = hit;
    }
  }
  return best;
}

bool Overlap(const Sphere& sphere, const Sphere& other) {
  const float radius_sum = sphere.radius + other.radius;
  return (sphere.center - other.center).GetSquaredLength() <= radius_sum * radius_sum;
}

bool Overlap(const Sphere& sphere, const Capsule& capsule) {
  const football::math::Vector3 closest = ClosestPointOnSegment(sphere.center, capsule.a, capsule.b);
  const float radius_sum = sphere.radius + capsule.radius;
  return (sphere.center - closest).GetSquaredLength() <= radius_sum * radius_sum;
}

bool Overlap(const Sphere& sphere, const Plane& plane) {
  const football::math::Vector3 n = NormalizedSafe(plane.normal, {0, 0, 1});
  // A plane is a surface, not a half-space: the sphere intersects it when its
  // center is within one radius of it, on either side.
  return std::fabs((sphere.center - plane.point).GetDotProduct(n)) <= sphere.radius;
}

}  // namespace football::collision
