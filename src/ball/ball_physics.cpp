// Ball integration: one tick of gravity/drag/spin/grass motion plus the
// ball-specific response to a contact. The continuous collision query itself
// lives in football::collision; this file only decides how the ball moves.

#include "football/ball/ball.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "football/ball/ball_response.hpp"
#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/math/scalar.hpp"

namespace football::ball {

using football::geometry::Capsule;
using football::geometry::ColliderMotion;
using football::geometry::ContactMaterial;
using football::geometry::HalfSpace;
using football::geometry::Plane;
using football::geometry::Sphere;

namespace {

constexpr float kEpsilon = 1e-6f;

// Long-lived physics parameters, derived from the ball's own model and the
// pitch it moves in. Internal to this kernel: BallModel/Pitch remain the only
// configuration surface, so there is no second set of exposed values.
struct Dynamics {
  float quadratic_resistance = 0.04f;        // air drag, from BallModel::drag()
  float ground_deceleration = 1.6f;          // rolling deceleration, from ground
  float quadratic_ground_resistance = 0.0f;  // speed-dependent ground resistance
  float grass_height = 0.025f;
  float spin_decay = 0.0f;
  float magnus_coefficient = 0.0f;           // from BallModel::magnus_coefficient()
};

Dynamics MakeDynamics(const football::ball::BallModel& config,
                      const football::pitch::Pitch& pitch) {
  Dynamics dynamics;
  dynamics.quadratic_resistance = config.drag();
  dynamics.ground_deceleration = pitch.ground_deceleration();
  dynamics.quadratic_ground_resistance = pitch.quadratic_resistance();
  dynamics.grass_height = pitch.grass_height();
  dynamics.spin_decay = 0.0f;
  dynamics.magnus_coefficient = config.magnus_coefficient();
  return dynamics;
}

// Lift the generic query result into the ball's contact evidence. The response
// layer needs the relative contact velocity, which the query deliberately does
// not compute (it only reports the surface velocity).
BallContact ToBallContact(const football::collision::CollisionHit& hit,
                          const football::math::Vector3& velocity) {
  BallContact contact;
  contact.collider = hit.collider;
  contact.toi = hit.toi;
  contact.point = hit.center;
  contact.normal = hit.normal;
  contact.surface_velocity = hit.surface_velocity;
  contact.relative_velocity = velocity - hit.surface_velocity;
  return contact;
}

}  // namespace

BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const ColliderMotion> colliders,
                               float dt,
                               const football::ball::BallModel& config,
                               const football::pitch::Pitch& pitch) {
  using football::collision::FirstHit;
  using football::collision::MovingSphere;

  const Dynamics dynamics = MakeDynamics(config, pitch);
  BallStepResult result;
  result.state = initial;
  const float eps = 1e-4f;
  const float ball_radius = config.radius();

  const auto sweep_from = [&](const BallState& state) {
    return FirstHit(MovingSphere{{state.position, ball_radius}, state.velocity},
                    colliders, dt);
  };

  const auto integrate_orientation = [](const football::math::Quaternion& q,
                                        const football::math::Vector3& omega, float dt) {
    const football::math::Quaternion omega_q(omega.coords[0], omega.coords[1], omega.coords[2], 0.0f);
    return (q + omega_q * q * (0.5f * dt)).GetNormalized();
  };

  // Resting contact that starts already penetrating must be projected back to
  // the surface before the impulse is applied; otherwise a ball below grass
  // level would keep bouncing in place without ever being separated.
  const auto depenetrate = [&](BallState& s, const BallContact& contact) {
    if (contact.toi > eps) return;
    for (const ColliderMotion& c : colliders) {
      if (c.id != contact.collider) continue;
      const football::math::Vector3 surface = std::visit(
          [&](const auto& shape) -> football::math::Vector3 {
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
              // Even a very early nonzero TOI must use the pose at that time,
              // never project a rotating/stretching capsule onto its START pose.
              const auto* end = std::get_if<Capsule>(&c.end);
              const auto a = end ? shape.a + (end->a-shape.a)*contact.toi : shape.a;
              const auto b = end ? shape.b + (end->b-shape.b)*contact.toi : shape.b;
              const float radius = end ? shape.radius + (end->radius-shape.radius)*contact.toi : shape.radius;
              const auto closest = football::collision::ClosestPointOnSegment(s.position-a, {0,0,0}, b-a);
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
    // Response must use the repaired center, never a stale pre-projection arm.
    contact.point = result.state.position;
    const auto response = ResolveContactResponse(result.state, contact, m,
                                                 contact.surface_velocity, config);
    result.state = response.state;
    contact.normal_impulse = response.normal_impulse;
    result.contacts.push_back(contact);
  };

  // P4e: a contact that only projects the ball out of an existing overlap must
  // not consume the whole interval. Position correction is instantaneous and
  // does not count as an impact, so the ball keeps its free motion and can still
  // find one genuine impact later in the same tick. At most one impulse
  // producing contact is reported per tick; corrections may be several.
  const auto separating = [](const BallContact& contact) {
    return contact.relative_velocity.GetDotProduct(contact.normal) >= 0.0f;
  };
  const auto correction_only = [&](const BallContact& contact) {
    return contact.toi <= eps && separating(contact);
  };
  // Position-only correction. Evidence stays in result.contacts with a zero
  // normal impulse so overlap projection is never mistaken for a rule touch.
  const auto project_out = [&](BallState& s, BallContact contact) {
    const auto original_center = s.position;
    depenetrate(s, contact);
    contact.position_corrected = s.position != original_center;
    contact.point = s.position;
    contact.normal_impulse = 0.0f;
    result.contacts.push_back(contact);
  };
  const std::size_t correction_cap = colliders.size() + 2;

  // Grass resistance applies in a thin zone above the ground; grass height
  // only widens the resistance zone, the support height is exactly the radius.
  const float resting_z = ball_radius;
  const float grass_zone = resting_z + dynamics.grass_height;
  const bool on_ground = initial.position.coords[2] <= resting_z + eps;
  const bool in_grass_zone = initial.position.coords[2] <= grass_zone + eps;
  const bool not_descending = initial.velocity.coords[2] >= -eps;

  const auto grass_drag = [&](const football::math::Vector3& velocity) {
    football::math::Vector3 v = velocity;
    const float hs =
        std::sqrt(v.coords[0] * v.coords[0] + v.coords[1] * v.coords[1]);
    if (hs > 0.0f) {
      const float resistance = dynamics.ground_deceleration +
                               dynamics.quadratic_ground_resistance * hs * hs;
      const float slowed = std::max(0.0f, hs - resistance * dt);
      const float scale = slowed / hs;
      v.coords[0] *= scale;
      v.coords[1] *= scale;
    }
    return v;
  };

  football::math::Vector3 grass_v = initial.velocity;
  if (in_grass_zone && not_descending) grass_v = grass_drag(initial.velocity);

  const float spin_scale = std::max(0.0f, 1.0f - dynamics.spin_decay * dt);
  const football::math::Vector3 omega1 = initial.angular_velocity * spin_scale;

  // Persistent ground contact: rolling constraint, but still check other
  // (non-ground) colliders; the tangent ground plane produces no ground hit.
  if (on_ground && not_descending) {
    const football::math::Vector3 v{grass_v.coords[0], grass_v.coords[1], 0.0f};
    BallState cursor = initial;
    cursor.velocity = v;
    cursor.position.coords[2] = resting_z;
    cursor.angular_velocity = omega1;
    const auto roll_free = [&] {
      result.state = cursor;
      result.state.velocity = v;
      result.state.position = cursor.position + v * dt;
      result.state.position.coords[2] = resting_z;
      result.state.angular_velocity = omega1;
      result.state.orientation =
          integrate_orientation(cursor.orientation, omega1, dt);
    };
    for (std::size_t guard = 0;; ++guard) {
      const auto hit = sweep_from(cursor);
      if (!hit.has_value()) {
        roll_free();
        return result;
      }
      const BallContact first = ToBallContact(*hit, cursor.velocity);
      if (!correction_only(first) || guard >= correction_cap) {
        result.state = cursor;
        result.state.position = first.point;
        result.state.velocity = v;
        resolve(first);
        return result;
      }
      project_out(cursor, first);
    }
  }

  // Airborne free-motion candidate. The launch velocity is recomputed from the
  // cursor so a corrected state still integrates its own drag and gravity.
  const football::math::Vector3 gravity = pitch.gravity();
  const auto launch_velocity = [&](const BallState& s) {
    if (s.position.coords[2] <= grass_zone + eps && s.velocity.coords[2] >= -eps)
      return grass_drag(s.velocity);
    return s.velocity;
  };
  const auto acceleration = [&](const football::math::Vector3& v0) {
    const float speed = v0.GetLength();
    football::math::Vector3 accel = gravity;
    if (speed > 0.0f)
      accel = accel - v0 * (dynamics.quadratic_resistance * speed);
    if (dynamics.magnus_coefficient != 0.0f) {
      const football::math::Vector3& o = initial.angular_velocity;
      accel = accel + football::math::Vector3(
          o.coords[1] * v0.coords[2] - o.coords[2] * v0.coords[1],
          o.coords[2] * v0.coords[0] - o.coords[0] * v0.coords[2],
          o.coords[0] * v0.coords[1] - o.coords[1] * v0.coords[0]) *
              dynamics.magnus_coefficient;
    }
    return accel;
  };

  BallState cursor = initial;
  for (std::size_t guard = 0;; ++guard) {
    const football::math::Vector3 v0 = launch_velocity(cursor);
    const football::math::Vector3 accel = acceleration(v0);
    const football::math::Vector3 vavg = v0 + accel * (0.5f * dt);
    BallState probe = cursor;
    probe.velocity = vavg;
    const auto hit = sweep_from(probe);
    if (!hit.has_value()) {
      result.state = cursor;
      result.state.position = cursor.position + vavg * dt;
      result.state.velocity = v0 + accel * dt;
      result.state.angular_velocity = omega1;
      result.state.orientation =
          integrate_orientation(cursor.orientation, omega1, dt);
      return result;
    }
    const BallContact first = ToBallContact(*hit, probe.velocity);
    if (!correction_only(first) || guard >= correction_cap) {
      // Move to the contact instant and let ResolveContact also spin the ball.
      const football::math::Vector3 vc = v0 + accel * (first.toi * dt);
      result.state = cursor;
      result.state.position = first.point;
      result.state.velocity = vc;
      result.state.angular_velocity = omega1;
      result.state.orientation = integrate_orientation(cursor.orientation,
          cursor.angular_velocity, first.toi * dt);
      resolve(first);
      return result;
    }
    project_out(cursor, first);
  }
}

}  // namespace football::ball
