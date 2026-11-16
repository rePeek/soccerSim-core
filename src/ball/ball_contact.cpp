#include "football/ball/ball_contact.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

#include "football/ball/ball_response.hpp"
#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"

namespace football::ball {

using geometry::Capsule;
using geometry::ColliderMotion;
using geometry::ContactMaterial;
using geometry::HalfSpace;
using geometry::Plane;
using geometry::Sphere;

namespace {

constexpr float kEpsilon = 1e-6f;
// Touching distance to the interval start and to the grass: closer than this is
// "already in contact", not a new impact.
constexpr float kTouchEpsilon = 1e-4f;

struct Dynamics {
  float quadratic_resistance = 0.04f;
  float ground_deceleration = 1.6f;
  float quadratic_ground_resistance = 0.0f;
  float spin_decay = 0.0f;
  float magnus_coefficient = 0.0f;
};

Dynamics MakeDynamics(const BallModel& config,
                      const Pitch& pitch) {
  Dynamics dynamics;
  dynamics.quadratic_resistance = pitch.air_drag();
  dynamics.ground_deceleration = pitch.ground_deceleration();
  dynamics.quadratic_ground_resistance = pitch.quadratic_resistance();
  dynamics.spin_decay = 0.0f;
  dynamics.magnus_coefficient = config.magnus_coefficient;
  return dynamics;
}

math::Quaternion IntegrateOrientation(const math::Quaternion& q,
                                      const math::Vector3& omega, float dt) {
  const math::Quaternion omega_q(omega.coords[0], omega.coords[1], omega.coords[2], 0.0f);
  return (q + omega_q * q * (0.5f * dt)).GetNormalized();
}

// Collision-free kinematics for one interval of a tick. The same rules drive a
// contact-free tick and the tail of a tick whose single impact is resolved.
struct Flight {
  Dynamics dynamics;
  math::Vector3 gravity;
  float ball_radius = 0.0f;
  float magnus_coefficient = 0.0f;

  bool OnGround(const BallState& state) const {
    return state.position.coords[2] <= ball_radius + kTouchEpsilon;
  }

  bool VerticallyResting(const BallState& state) const {
    return std::fabs(state.velocity.coords[2]) <= kTouchEpsilon;
  }

  // Grass resists horizontal motion only: a constant deceleration plus an
  // optional quadratic term, clamping at rest instead of flipping the sign.
  math::Vector3 GrassDrag(const math::Vector3& velocity, float span) const {
    math::Vector3 v = velocity;
    const float hs =
        std::sqrt(v.coords[0] * v.coords[0] + v.coords[1] * v.coords[1]);
    if (hs > 0.0f) {
      const float resistance = dynamics.ground_deceleration +
                               dynamics.quadratic_ground_resistance * hs * hs;
      const float scale = std::max(0.0f, hs - resistance * span) / hs;
      v.coords[0] *= scale;
      v.coords[1] *= scale;
    }
    return v;
  }

  // Ground resistance applies only while the ball rests on the grass and is not
  // climbing; a descended interval keeps the velocity it has.
  math::Vector3 LaunchVelocity(const BallState& state, float span) const {
    if (OnGround(state) && state.velocity.coords[2] >= -kTouchEpsilon)
      return GrassDrag(state.velocity, span);
    return state.velocity;
  }

  math::Vector3 Acceleration(const math::Vector3& v0,
                             const math::Vector3& omega) const {
    const float speed = v0.GetLength();
    math::Vector3 accel = gravity;
    if (speed > 0.0f)
      accel = accel - v0 * (dynamics.quadratic_resistance * speed);
    if (magnus_coefficient != 0.0f) {
      accel = accel + math::Vector3(
                          omega.coords[1] * v0.coords[2] - omega.coords[2] * v0.coords[1],
                          omega.coords[2] * v0.coords[0] - omega.coords[0] * v0.coords[2],
                          omega.coords[0] * v0.coords[1] - omega.coords[1] * v0.coords[0]) *
                          magnus_coefficient;
    }
    return accel;
  }

  // One interval with no collision query: rolling when the ball rests on the
  // grass, ballistic (half-step velocity) otherwise.
  BallState Advance(const BallState& state, float span) const {
    if (span <= 0.0f) return state;
    BallState next = state;
    next.angular_velocity =
        state.angular_velocity * std::max(0.0f, 1.0f - dynamics.spin_decay * span);
    if (OnGround(state) && VerticallyResting(state)) {
      next.velocity = GrassDrag(state.velocity, span);
      next.velocity.coords[2] = 0.0f;
      next.position = state.position + next.velocity * span;
      next.position.coords[2] = ball_radius;
    } else {
      const math::Vector3 v0 = LaunchVelocity(state, span);
      const math::Vector3 accel = Acceleration(v0, state.angular_velocity);
      next.position = state.position + (v0 + accel * (0.5f * span)) * span;
      next.velocity = v0 + accel * span;
    }
    next.orientation = IntegrateOrientation(state.orientation, next.angular_velocity, span);
    return next;
  }
};

Flight MakeFlight(const BallModel& config, const Pitch& pitch) {
  Flight flight;
  flight.dynamics = MakeDynamics(config, pitch);
  flight.gravity = math::Vector3(0.0f, 0.0f, pitch.gravity());
  flight.ball_radius = config.radius;
  flight.magnus_coefficient = config.magnus_coefficient;
  return flight;
}

BallContact ToBallContact(const collision::CollisionHit& hit,
                          const math::Vector3& velocity) {
  BallContact contact;
  contact.collider = hit.collider;
  contact.toi = hit.toi;
  contact.point = hit.center;
  contact.normal = hit.normal;
  contact.surface_velocity = hit.surface_velocity;
  contact.relative_velocity = velocity - hit.surface_velocity;
  return contact;
}

// One interval [0, dt]: resolve at most one contact, leaving the caller to fly
// the rest of the tick collision-free.
BallStepResult AdvanceBallSegment(const BallState& initial,
                               std::span<const ColliderMotion> colliders,
                               float dt,
                               const BallModel& config,
                               const Pitch& pitch) {
  using collision::FirstHit;
  using collision::MovingSphere;

  const Flight flight = MakeFlight(config, pitch);
  const float eps = kTouchEpsilon;
  const float ball_radius = config.radius;

  BallStepResult result;
  result.state = initial;

  std::vector<geometry::ColliderId> tangent_contacts;
  const auto sweep_from = [&](const BallState& state) {
    // Common case: no repaired tangent yet, so the earliest hit over the whole
    // span is exact and one sweep is enough. Only after an overlap projection
    // is a per-collider scan needed to keep a repaired, separating tangent from
    // hiding another collider.
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

  const auto resolve = [&](BallContact contact) {
    const ContactMaterial* material = nullptr;
    for (const ColliderMotion& c : colliders) {
      if (c.id == contact.collider) { material = &c.material; break; }
    }
    const ContactMaterial m = material ? *material : ContactMaterial{0.5f, 0.0f};
    const auto original_center = result.state.position;
    depenetrate(result.state, contact);
    contact.position_corrected = result.state.position != original_center;
    contact.point = result.state.position;
    const auto response = ResolveContactResponse(result.state, contact, m,
                                                 contact.surface_velocity, config);
    result.state = response.state;
    contact.normal_impulse = response.normal_impulse;
    result.contacts.push_back(contact);
  };

  const auto separating = [](const BallContact& contact) {
    return contact.relative_velocity.GetDotProduct(contact.normal) >= 0.0f;
  };
  const auto correction_only = [&](const BallContact& contact) {
    return contact.toi <= eps && separating(contact);
  };
  const auto project_out = [&](BallState& s, BallContact contact) {
    const auto original_center = s.position;
    depenetrate(s, contact);
    contact.position_corrected = s.position != original_center;
    tangent_contacts.push_back(contact.collider);
    contact.point = s.position;
    contact.normal_impulse = 0.0f;
    result.contacts.push_back(contact);
  };
  const std::size_t correction_cap = colliders.size() + 2;

  // Rolling: the ball rests on the grass, so the tick is two-dimensional. The
  // sweep follows the dragged roll instead of the raw velocity.
  if (flight.OnGround(initial) && flight.VerticallyResting(initial)) {
    BallState cursor = initial;
    cursor.position.coords[2] = ball_radius;
    cursor.velocity.coords[2] = 0.0f;
    const math::Vector3 roll_velocity = flight.GrassDrag(cursor.velocity, dt);
    for (std::size_t guard = 0;; ++guard) {
      BallState probe = cursor;
      probe.velocity = roll_velocity;
      const auto hit = sweep_from(probe);
      if (!hit.has_value()) {
        result.state = flight.Advance(cursor, dt);
        return result;
      }
      auto impact_velocity = flight.GrassDrag(initial.velocity, hit->toi * dt);
      impact_velocity.coords[2] = 0.0f;
      const BallContact first = ToBallContact(*hit, impact_velocity);
      if (!correction_only(first) || guard >= correction_cap) {
        result.state = cursor;
        result.state.position = first.point;
        result.state.velocity = impact_velocity;
        result.state.orientation = IntegrateOrientation(
            cursor.orientation, cursor.angular_velocity, first.toi * dt);
        resolve(first);
        return result;
      }
      project_out(cursor, first);
    }
  }

  BallState cursor = initial;
  for (std::size_t guard = 0;; ++guard) {
    const math::Vector3 v0 = flight.LaunchVelocity(cursor, dt);
    const math::Vector3 accel = flight.Acceleration(v0, cursor.angular_velocity);
    BallState probe = cursor;
    probe.velocity = v0 + accel * (0.5f * dt);
    const auto hit = sweep_from(probe);
    if (!hit.has_value()) {
      result.state = flight.Advance(cursor, dt);
      return result;
    }
    const math::Vector3 vc = v0 + accel * (hit->toi * dt);
    BallContact first = ToBallContact(*hit, vc);
    if (!correction_only(first) || guard >= correction_cap) {
      result.state = cursor;
      result.state.position = first.point;
      result.state.velocity = vc;
      result.state.orientation = IntegrateOrientation(cursor.orientation,
          cursor.angular_velocity, first.toi * dt);
      resolve(first);
      return result;
    }
    project_out(cursor, first);
  }
}

}  // namespace

BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const ColliderMotion> colliders,
                               float dt, const BallModel& config,
                               const Pitch& pitch) {
  BallStepResult result;
  result.state = initial;
  if (dt <= 0.0f) return result;

  // Safety bound on the splits before the tick's impact: the apex clip below
  // needs at most one, and each overlap projection spends one more.
  constexpr int kMaxSegments = 32;
  const Flight flight = MakeFlight(config, pitch);

  float elapsed = 0.0f;
  std::vector<ColliderMotion> interval;
  for (int step = 0; step < kMaxSegments && elapsed < dt; ++step) {
    float duration = dt - elapsed;
    const auto& v = result.state.velocity;
    const auto& w = result.state.angular_velocity;
    const float az = pitch.gravity() -
        pitch.air_drag() * v.GetLength() * v.coords[2] +
        config.magnus_coefficient *
            (w.coords[0] * v.coords[1] - w.coords[1] * v.coords[0]);
    if (v.coords[2] > 1e-4f && az < 0.0f)
      duration = std::min(duration, -v.coords[2] / az);
    // The first segment already carries the full-tick collider start/end poses,
    // so it uses the caller's span directly. Only a remainder segment needs the
    // interpolated interval copy; most ticks have no impact and never pay it.
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
    auto segment = AdvanceBallSegment(result.state, segment_colliders, duration, config, pitch);
    result.state = segment.state;
    bool collided = false;
    float consumed = duration;
    for (auto contact : segment.contacts) {
      if (contact.normal_impulse > 0.0f) {
        collided = true;
        consumed = duration * contact.toi;
      }
      contact.toi = (elapsed + duration * contact.toi) / dt;
      result.contacts.push_back(contact);
    }
    elapsed += consumed;
    if (!collided) continue;
    // One impact per tick: the rest of the tick is collision-free, so a surface
    // reached afterwards is left to the next tick. A thin collider entered only
    // during this tail is therefore not seen now; the next tick starts inside
    // it and repairs the overlap instead of accepting a tunnel.
    result.state = flight.Advance(result.state, dt - elapsed);
    break;
  }
  return result;
}

}  // namespace football::ball
