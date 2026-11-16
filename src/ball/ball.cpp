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
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
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

// ============================================================================
// Impulse response
// ============================================================================

float BallInertia(const BallModel& config) {
  return kBallInertiaFactor * config.mass * config.radius * config.radius;
}

BallState ApplyImpulseAtPoint(const BallState& ball, const math::Vector3& impulse,
                              const math::Vector3& world_point,
                              const BallModel& config) {
  BallState next = ball;
  next.velocity = next.velocity + impulse * (1.0f / config.mass);

  const math::Vector3 r = world_point - ball.position;
  const math::Vector3 torque(
      r.coords[1] * impulse.coords[2] - r.coords[2] * impulse.coords[1],
      r.coords[2] * impulse.coords[0] - r.coords[0] * impulse.coords[2],
      r.coords[0] * impulse.coords[1] - r.coords[1] * impulse.coords[0]);
  next.angular_velocity = next.angular_velocity + torque * (1.0f / BallInertia(config));
  return next;
}

ContactImpulse SolveContactImpulse(const BallState& ball, const BallContact& contact,
                                   const ContactMaterial& material,
                                   const BallModel& config) {
  const math::Vector3 n = contact.normal.GetNormalized({0, 0, 1});
  const math::Vector3 r = contact.point - ball.position;

  const math::Vector3& omega = ball.angular_velocity;
  const math::Vector3 omega_cross_r(
      omega.coords[1] * r.coords[2] - omega.coords[2] * r.coords[1],
      omega.coords[2] * r.coords[0] - omega.coords[0] * r.coords[2],
      omega.coords[0] * r.coords[1] - omega.coords[1] * r.coords[0]);

  const math::Vector3 u = ball.velocity + omega_cross_r - contact.surface_velocity;
  const float un = u.GetDotProduct(n);
  if (un >= 0.0f) return {};

  const float mass = config.mass;
  const float jn = -mass * (1.0f + material.restitution) * un;

  // Tangential effective mass along the contact plane: the lever arm cancels
  // the rotation only while it is perpendicular to the slip, which a sphere
  // contact point always is.
  const math::Vector3 ut = u - n * un;
  const float inverse =
      1.0f / mass + r.GetDotProduct(r) / BallInertia(config);
  math::Vector3 jt = ut * (-1.0f / inverse);

  const float jt_len = jt.GetLength();
  const float mu_jn = material.friction * jn;
  if (jt_len > mu_jn) jt = jt * (mu_jn / jt_len);

  return {n * jn + jt, jn};
}

namespace {

// ============================================================================
// Free-flight kinematics
// ============================================================================

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

// ============================================================================
// Contact resolution for one interval
// ============================================================================

BallContact ToBallContact(const collision::CollisionHit& hit,
                          const math::Vector3& velocity, float ball_radius) {
  BallContact contact;
  contact.collider = hit.collider;
  contact.toi = hit.toi;
  contact.point = hit.center - hit.normal * ball_radius;
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
    // Depenetration moves the centre along the normal, so the contact point
    // stays on the collider surface and the lever arm stays -normal * radius.
    contact.point = result.state.position - contact.normal * ball_radius;
    const ContactImpulse solved = SolveContactImpulse(result.state, contact, m, config);
    contact.normal_impulse = solved.normal_impulse;
    if (solved.normal_impulse > 0.0f)
      result.state =
          ApplyImpulseAtPoint(result.state, solved.impulse, contact.point, config);
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
    contact.point = s.position - contact.normal * ball_radius;
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
      const BallContact first = ToBallContact(*hit, impact_velocity, ball_radius);
      if (!correction_only(first) || guard >= correction_cap) {
        result.state = cursor;
        result.state.position = hit->center;
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
    BallContact first = ToBallContact(*hit, vc, ball_radius);
    if (!correction_only(first) || guard >= correction_cap) {
      result.state = cursor;
      result.state.position = hit->center;
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

// ============================================================================
// One tick
// ============================================================================

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

// ============================================================================
// Ball
// ============================================================================

Ball::Ball(const BallModel& model)
    : model_(model), state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {
  Info(Channel::ball,
       std::format("ball created: mass={} radius={} magnus={}", model_.mass,
                   model_.radius, model_.magnus_coefficient));
}

BallStepResult Ball::EvaluateTick(const Pitch& pitch) const {
  return AdvanceBallTick(
      state_, pitch.colliders(), time::kTickSeconds, model_, pitch);
}

BallStepResult Ball::Predict(const Pitch& pitch) const {
  return EvaluateTick(pitch);
}

BallStepResult Ball::Step(const Pitch& pitch) {
  auto result = EvaluateTick(pitch);
  state_ = result.state;
  if (!result.contacts.empty()) {
    Debug(Channel::ball, std::format("tick resolved {} contact(s); position=({}, {}, {})",
                                     result.contacts.size(), state_.position.coords[0],
                                     state_.position.coords[1], state_.position.coords[2]));
  }
  return result;
}

BallState Ball::Predict(const Pitch& pitch,
                        time::TickSpan ahead) const {
  BallState predicted = state_;
  for (time::TickSpan i{0}; i < ahead; i += time::TickSpan{1}) {
    predicted = AdvanceBallTick(
        predicted, pitch.colliders(), time::kTickSeconds, model_, pitch).state;
  }
  return predicted;
}

void Ball::PlaceOnGround(float x, float y) {
  BallState state;
  state.position = math::Vector3(x, y, model_.radius);
  set_state(state);
  Info(Channel::ball, std::format("placed on ground at ({}, {})", x, y));
}

}  // namespace football::ball
