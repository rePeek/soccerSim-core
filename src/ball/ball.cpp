#include "football/ball/ball.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/log/log.hpp"
#include "football/pitch/pitch.hpp"

using namespace football::log;
using namespace football::math;

namespace football::ball {

using geometry::ContactMaterial;

namespace {

constexpr float kGroundContactDistanceTolerance = 1e-4f;
constexpr float kDescendingSpeedTolerance = 1e-4f;
constexpr float kRestLinearSpeedTolerance = 1e-4f;
constexpr float kRestAngularSpeedTolerance = 1e-4f;
constexpr float kShellRadiusRelativeTolerance = 1e-3f;
constexpr float kMaxRollingVerticalSpeed = 0.1f;

float ThinShellMomentOfInertia(const BallModel& model) {
  return (2.0f / 3.0f) * model.mass * model.radius * model.radius;
}

bool IsRolling(const BallModel& model, const BallState& state) {
  return state.position.coords[2] <= model.radius + kGroundContactDistanceTolerance &&
         std::fabs(state.velocity.coords[2]) <= kMaxRollingVerticalSpeed;
}

math::Vector3 ApplyGrassResistance(const math::Vector3& velocity,
                                  float dt, const Pitch& pitch) {
  math::Vector3 slowed_velocity = velocity;
  const float horizontal_speed =
      std::sqrt(slowed_velocity.coords[0] * slowed_velocity.coords[0] +
                slowed_velocity.coords[1] * slowed_velocity.coords[1]);
  if (horizontal_speed > 0.0f) {
    const float deceleration = pitch.ground_deceleration() +
                              pitch.quadratic_resistance() * horizontal_speed * horizontal_speed;
    const float speed_scale =
        std::max(0.0f, horizontal_speed - deceleration * dt) / horizontal_speed;
    slowed_velocity.coords[0] *= speed_scale;
    slowed_velocity.coords[1] *= speed_scale;
  }
  return slowed_velocity;
}

math::Vector3 LaunchVelocity(const BallModel& model, const BallState& state,
                             float dt, const Pitch& pitch) {
  if (state.position.coords[2] <= model.radius + kGroundContactDistanceTolerance &&
      state.velocity.coords[2] >= -kDescendingSpeedTolerance) {
    return ApplyGrassResistance(state.velocity, dt, pitch);
  }
  return state.velocity;
}

math::Vector3 FlightAcceleration(const BallModel& model, const math::Vector3& velocity,
                                 const math::Vector3& angular_velocity, const Pitch& pitch) {
  const float speed = velocity.GetLength();
  math::Vector3 acceleration{0.0f, 0.0f, pitch.gravity()};
  if (speed > 0.0f) {
    acceleration = acceleration - velocity * (pitch.air_drag() * speed);
  }
  if (model.magnus_coefficient != 0.0f) {
    acceleration = acceleration +
                   angular_velocity.GetCrossProduct(velocity) * model.magnus_coefficient;
  }
  return acceleration;
}

bool IsAtRest(const BallState& state) {
  return state.velocity.GetSquaredLength() <=
             kRestLinearSpeedTolerance * kRestLinearSpeedTolerance &&
         state.angular_velocity.GetSquaredLength() <=
             kRestAngularSpeedTolerance * kRestAngularSpeedTolerance;
}

BallState IntegrateMotion(const BallModel& model, const BallState& state,
                          float dt, const Pitch& pitch) {
  if (dt <= 0.0f) return state;
  const bool rolling = IsRolling(model, state);
  if (rolling && IsAtRest(state)) return state;
  BallState next = state;
  if (rolling) {
    next.velocity = ApplyGrassResistance(state.velocity, dt, pitch);
    next.velocity.coords[2] = 0.0f;
    next.position = state.position + next.velocity * dt;
    next.position.coords[2] = model.radius;
  } else {
    const math::Vector3 launch_velocity = LaunchVelocity(model, state, dt, pitch);
    const math::Vector3 acceleration =
        FlightAcceleration(model, launch_velocity, state.angular_velocity, pitch);
    next.position = state.position +
                    (launch_velocity + acceleration * (0.5f * dt)) * dt;
    next.velocity = launch_velocity + acceleration * dt;
  }
  next.orientation =
      IntegrateOrientation(state.orientation, next.angular_velocity, dt);
  return next;
}

}

Ball::Ball(const BallModel& model)
    : model_(model), state_{Vector3(0), Vector3(0), Vector3(0), Quaternion()} {
  Info(Channel::ball,
       std::format("ball created: mass={} radius={} magnus={}", model_.mass,
                   model_.radius, model_.magnus_coefficient));
}

void Ball::Step(const Pitch& pitch) {
  constexpr float dt = time::kTickSeconds;
  const float ball_radius = model_.radius;
  const BallState start = state_;

  BallState predicted = IntegrateMotion(model_, start, dt, pitch);

  const auto colliders = pitch.colliders();
  const auto swept = IsRolling(model_, start) ? colliders.subspan(1) : colliders;
  const collision::MovingSphere sphere{
      {start.position, ball_radius},
      (predicted.position - start.position) * (1.0f / dt)};
  const auto found = collision::FirstHit(sphere, swept, dt);
  if (!found) {
    set_state(predicted);
    return;
  }

  const auto& hit = found->hit;
  predicted.position = hit.center;
  set_state(predicted);

  const geometry::Collider& collider = *found->collider;
  const math::Vector3 contact_point = predicted.position - hit.normal * ball_radius;
  const auto impulse = SolveContactImpulse(
      hit.normal, contact_point, hit.surface_velocity, collider.material);
  if (impulse) ApplyImpulseAtPoint(*impulse, contact_point);
  Debug(Channel::ball,
        std::format("ball contact: name={}, impact_fraction={}",
                    collider.name, hit.impact_fraction));
}

void Ball::ApplyImpulseAtPoint(const math::Vector3& impulse,
                               const math::Vector3& world_point) {
  const math::Vector3 lever_arm = world_point - state_.position;
  assert(std::fabs(lever_arm.GetLength() - model_.radius) <=
         kShellRadiusRelativeTolerance * model_.radius);

  BallState next = state_;
  next.velocity = next.velocity + impulse * (1.0f / model_.mass);

  const math::Vector3 angular_impulse(lever_arm.GetCrossProduct(impulse));
  next.angular_velocity = next.angular_velocity +
                          angular_impulse * (1.0f / ThinShellMomentOfInertia(model_));
  set_state(next);
}

std::optional<math::Vector3> Ball::SolveContactImpulse(
    const math::Vector3& normal, const math::Vector3& contact_point,
    const math::Vector3& surface_velocity, const ContactMaterial& material) const {
  const math::Vector3 unit_normal = normal.GetNormalized({0, 0, 1});
  const math::Vector3 lever_arm = contact_point - state_.position;

  const math::Vector3& angular_velocity = state_.angular_velocity;
  const math::Vector3 rotational_contact_velocity = angular_velocity.GetCrossProduct(lever_arm);

  const math::Vector3 relative_contact_velocity =
      state_.velocity + rotational_contact_velocity - surface_velocity;
  const float normal_speed = relative_contact_velocity.GetDotProduct(unit_normal);
  if (normal_speed >= 0.0f) return std::nullopt;

  const float mass = model_.mass;
  const float normal_impulse = -mass * (1.0f + material.restitution) * normal_speed;

  const math::Vector3 tangential_velocity = relative_contact_velocity - unit_normal * normal_speed;
  const float inverse_tangential_effective_mass =
      1.0f / mass + lever_arm.GetDotProduct(lever_arm) / ThinShellMomentOfInertia(model_);
  math::Vector3 friction_impulse = tangential_velocity * (-1.0f / inverse_tangential_effective_mass);

  const float friction_impulse_length = friction_impulse.GetLength();
  const float max_friction_impulse = material.friction * normal_impulse;
  if (friction_impulse_length > max_friction_impulse) {
    friction_impulse = friction_impulse * (max_friction_impulse / friction_impulse_length);
  }

  return unit_normal * normal_impulse + friction_impulse;
}

void Ball::PlaceOnGround(float x, float y) {
  BallState state;
  state.position = math::Vector3(x, y, model_.radius);
  set_state(state);
  Info(Channel::ball, std::format("placed on ground at ({}, {})", x, y));
}

}
