#include "football/ball/ball_contact.hpp"

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
  if (d0 <= kEpsilon) {
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

  if (dist0 <= radius_sum + kEpsilon) {
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

  if (dist0 <= radius_sum + kEpsilon) {
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

}  // namespace football::ball