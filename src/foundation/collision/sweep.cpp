#include "football/foundation/collision/sweep.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/scalar.hpp"

namespace football::collision {

using geometry::Capsule;
using geometry::Collider;
using geometry::ColliderShape;
using geometry::HalfSpace;
using geometry::Plane;
using geometry::Sphere;

math::Vector3 ClosestPointOnSegment(const math::Vector3& p,
                                       const math::Vector3& a,
                                       const math::Vector3& b) {
  const math::Vector3 ab = b - a;
  const float len2 = ab.GetDotProduct(ab);
  if (len2 == 0.0f) return a;
  float t = (p - a).GetDotProduct(ab) / len2;
  t = math::clamp(t, 0.0f, 1.0f);
  return a + ab * t;
}

math::Vector3 Depenetrate(const math::Vector3& center, float radius,
                          const geometry::ColliderShape& shape,
                          const math::Vector3& normal) {
  const math::Vector3 surface = std::visit(
      [&](const auto& concrete) -> math::Vector3 {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, Plane> || std::is_same_v<T, HalfSpace>) {
          const auto n = concrete.normal.GetNormalized({0, 0, 1});
          return center - n * (center - concrete.point).GetDotProduct(n);
        } else if constexpr (std::is_same_v<T, Sphere>) {
          return concrete.center + normal * concrete.radius;
        } else {
          const auto closest = ClosestPointOnSegment(
              center - concrete.a, {0, 0, 0}, concrete.b - concrete.a);
          return concrete.a + closest + normal * concrete.radius;
        }
      },
      shape);
  return surface + normal * radius;
}


namespace {

constexpr float kEpsilon = 1e-6f;

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

math::Vector3 NormalizedSafe(const math::Vector3& v,
                                const math::Vector3& fallback) {
  return v.GetNormalized(fallback);
}

bool IsFiniteVector(const math::Vector3& v) {
  return std::isfinite(v.coords[0]) && std::isfinite(v.coords[1]) &&
         std::isfinite(v.coords[2]);
}

void ValidateShape(const ColliderShape& shape) {
  std::visit(
      [](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Plane> || std::is_same_v<T, HalfSpace>) {
          if (!IsFiniteVector(s.point) || !IsFiniteVector(s.normal))
            throw std::invalid_argument(
                "collision::Sweep: non-finite plane/half-space");
        } else if constexpr (std::is_same_v<T, Sphere>) {
          if (!std::isfinite(s.radius) || s.radius < 0.0f)
            throw std::invalid_argument(
                "collision::Sweep: invalid sphere radius");
          if (!IsFiniteVector(s.center))
            throw std::invalid_argument(
                "collision::Sweep: non-finite sphere centre");
        } else {
          if (!std::isfinite(s.radius) || s.radius < 0.0f)
            throw std::invalid_argument(
                "collision::Sweep: invalid capsule radius");
          if (!IsFiniteVector(s.a) || !IsFiniteVector(s.b))
            throw std::invalid_argument(
                "collision::Sweep: non-finite capsule axis");
        }
      },
      shape);
}

void ValidateSweep(const MovingSphere& sphere, const ColliderShape& shape,
                   float dt) {
  if (!std::isfinite(dt) || dt <= 0.0f)
    throw std::invalid_argument(
        "collision::Sweep: dt must be finite and > 0");
  if (!std::isfinite(sphere.shape.radius) || sphere.shape.radius < 0.0f ||
      !IsFiniteVector(sphere.shape.center) || !IsFiniteVector(sphere.velocity))
    throw std::invalid_argument(
        "collision::Sweep: invalid moving sphere");
  ValidateShape(shape);
}

std::optional<CollisionHit> SweepPlane(const MovingSphere& sphere,
                                       const Plane& plane, float dt) {
  const math::Vector3 ball_position = sphere.shape.center;
  const math::Vector3 disp = sphere.velocity * dt;
  const float ball_radius = sphere.shape.radius;

  const math::Vector3 n = NormalizedSafe(plane.normal, {0, 0, 1});
  const float d0 = (ball_position - plane.point).GetDotProduct(n);
  const float dn = disp.GetDotProduct(n);

  CollisionHit hit;
  if (std::fabs(d0) <= ball_radius + kEpsilon) {
    hit.impact_fraction = 0.0f;
    hit.center = ball_position;
    hit.normal = (d0 >= 0.0f) ? n : -n;
    return hit;
  }
  if (std::fabs(dn) < kEpsilon) return std::nullopt;
  const float target = (d0 > 0.0f) ? ball_radius : -ball_radius;
  const float t = (target - d0) / dn;
  if (t < 0.0f || t > 1.0f) return std::nullopt;
  hit.impact_fraction = t;
  hit.center = ball_position + disp * t;
  hit.normal = (d0 > 0.0f) ? n : -n;
  return hit;
}

std::optional<CollisionHit> SweepHalfSpace(const MovingSphere& sphere,
                                           const HalfSpace& half_space, float dt) {
  const math::Vector3 ball_position = sphere.shape.center;
  const math::Vector3 disp = sphere.velocity * dt;
  const float ball_radius = sphere.shape.radius;

  const math::Vector3 n =
      NormalizedSafe(half_space.normal, {0, 0, 1});
  const float d0 =
      (ball_position - half_space.point).GetDotProduct(n) - ball_radius;
  const float dn = disp.GetDotProduct(n);

  CollisionHit hit;
  hit.normal = n;
  if (d0 < -kEpsilon) {
    hit.impact_fraction = 0.0f;
    hit.center = ball_position;
    return hit;
  }
  if (dn >= 0.0f) return std::nullopt;
  const float t = -d0 / dn;
  if (t < 0.0f || t > 1.0f) return std::nullopt;
  hit.impact_fraction = t;
  hit.center = ball_position + disp * t;
  return hit;
}

std::optional<CollisionHit> SweepSphere(const MovingSphere& sphere,
                                        const Sphere& other, float dt) {
  const math::Vector3 ball_position = sphere.shape.center;
  const math::Vector3 disp = sphere.velocity * dt;
  const float ball_radius = sphere.shape.radius;

  const math::Vector3 r0 = ball_position - other.center;
  const float radius_sum = ball_radius + other.radius;

  CollisionHit hit;

  if (r0.GetLength() < radius_sum - kEpsilon) {
    hit.impact_fraction = 0.0f;
    hit.center = ball_position;
    hit.normal = NormalizedSafe(r0, {0, 0, 1});
    return hit;
  }
  if (r0.GetDotProduct(disp) >= 0.0f) return std::nullopt;

  const float a = disp.GetDotProduct(disp);
  const float b = 2.0f * r0.GetDotProduct(disp);
  const float c = r0.GetDotProduct(r0) - radius_sum * radius_sum;
  float t0, t1;
  const int roots = SolveQuadratic(a, b, c, t0, t1);
  if (roots == 0) return std::nullopt;
  const float t = (t0 >= 0.0f) ? t0 : t1;
  if (t < 0.0f || t > 1.0f) return std::nullopt;
  const math::Vector3 center_ball = ball_position + disp * t;
  hit.impact_fraction = t;
  hit.center = center_ball;
  hit.normal = NormalizedSafe(center_ball - other.center, {0, 0, 1});
  return hit;
}

std::optional<CollisionHit> SweepCapsule(const MovingSphere& sphere,
                                         const Capsule& capsule, float dt) {
  const math::Vector3 ball_position = sphere.shape.center;
  const math::Vector3 disp = sphere.velocity * dt;
  const float ball_radius = sphere.shape.radius;

  const math::Vector3 r0 = ball_position - capsule.a;
  const math::Vector3 seg_a{0, 0, 0};
  const math::Vector3 seg_b = capsule.b - capsule.a;
  const float radius_sum = ball_radius + capsule.radius;

  const math::Vector3 closest0 = ClosestPointOnSegment(r0, seg_a, seg_b);
  const float dist0 = (r0 - closest0).GetLength();

  CollisionHit hit;

  if (dist0 < radius_sum - kEpsilon) {
    hit.impact_fraction = 0.0f;
    hit.center = ball_position;
    hit.normal = NormalizedSafe(r0 - closest0, {0, 0, 1});
    return hit;
  }
  if ((r0 - closest0).GetDotProduct(disp) >= 0.0f) return std::nullopt;

  float best = std::numeric_limits<float>::max();
  for (const math::Vector3& center : {seg_a, seg_b}) {
    const math::Vector3 rr0 = r0 - center;
    const float a = disp.GetDotProduct(disp);
    const float b = 2.0f * rr0.GetDotProduct(disp);
    const float c = rr0.GetDotProduct(rr0) - radius_sum * radius_sum;
    float t0, t1;
    const int roots = SolveQuadratic(a, b, c, t0, t1);
    if (roots == 0) continue;
    const float t = (t0 >= 0.0f) ? t0 : t1;
    if (t >= 0.0f && t <= 1.0f) best = std::min(best, t);
  }

  const math::Vector3 dir_v = seg_b - seg_a;
  const float dir_len2 = dir_v.GetDotProduct(dir_v);
  if (dir_len2 > kEpsilon) {
    const math::Vector3 d = dir_v * (1.0f / std::sqrt(dir_len2));
    const float ca = disp.GetDotProduct(disp) - std::pow(disp.GetDotProduct(d), 2.0f);
    const float cb = 2.0f * (r0.GetDotProduct(disp) - r0.GetDotProduct(d) * disp.GetDotProduct(d));
    const float cc = r0.GetDotProduct(r0) - std::pow(r0.GetDotProduct(d), 2.0f) - radius_sum * radius_sum;
    float t0, t1;
    const int roots = SolveQuadratic(ca, cb, cc, t0, t1);
    if (roots > 0) {
      const float t = (t0 >= 0.0f) ? t0 : t1;
      if (t >= 0.0f && t <= 1.0f) {
        const math::Vector3 at = r0 + disp * t;
        const float s = at.GetDotProduct(d);
        if (s >= -kEpsilon && s <= std::sqrt(dir_len2) + kEpsilon) {
          best = std::min(best, t);
        }
      }
    }
  }

  if (best > 1.0f) return std::nullopt;
  const math::Vector3 center_ball = ball_position + disp * best;
  const math::Vector3 r_at = r0 + disp * best;
  const math::Vector3 closest = ClosestPointOnSegment(r_at, seg_a, seg_b);
  hit.impact_fraction = best;
  hit.center = center_ball;
  hit.normal = NormalizedSafe(r_at - closest, {0, 0, 1});
  return hit;
}

}

std::optional<CollisionHit> Sweep(const MovingSphere& sphere,
                                  const ColliderShape& shape, float dt) {
  ValidateSweep(sphere, shape, dt);
  return std::visit(
      [&](const auto& concrete) -> std::optional<CollisionHit> {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, Plane>) {
          return SweepPlane(sphere, concrete, dt);
        } else if constexpr (std::is_same_v<T, HalfSpace>) {
          return SweepHalfSpace(sphere, concrete, dt);
        } else if constexpr (std::is_same_v<T, Sphere>) {
          return SweepSphere(sphere, concrete, dt);
        } else {
          return SweepCapsule(sphere, concrete, dt);
        }
      },
      shape);
}

std::optional<ColliderHit> FirstHit(
    const MovingSphere& sphere,
    std::span<const Collider> colliders, float dt) {
  std::optional<ColliderHit> best;
  for (const Collider& collider : colliders) {
    const std::optional<CollisionHit> hit = Sweep(sphere, collider.shape, dt);
    if (!hit.has_value()) continue;
    if (!best.has_value() || hit->impact_fraction < best->hit.impact_fraction)
      best = ColliderHit{*hit, &collider};
  }
  return best;
}

bool Overlap(const Sphere& sphere, const Sphere& other) {
  const float radius_sum = sphere.radius + other.radius;
  return (sphere.center - other.center).GetSquaredLength() <= radius_sum * radius_sum;
}

bool Overlap(const Sphere& sphere, const Capsule& capsule) {
  const math::Vector3 closest = ClosestPointOnSegment(sphere.center, capsule.a, capsule.b);
  const float radius_sum = sphere.radius + capsule.radius;
  return (sphere.center - closest).GetSquaredLength() <= radius_sum * radius_sum;
}

bool Overlap(const Sphere& sphere, const Plane& plane) {
  const math::Vector3 n = NormalizedSafe(plane.normal, {0, 0, 1});
  return std::fabs((sphere.center - plane.point).GetDotProduct(n)) <= sphere.radius;
}

bool Overlap(const Sphere& sphere, const HalfSpace& half_space) {
  const math::Vector3 n =
      NormalizedSafe(half_space.normal, {0, 0, 1});
  return (sphere.center - half_space.point).GetDotProduct(n) <= sphere.radius;
}

}
