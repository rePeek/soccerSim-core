#ifndef FOOTBALL_PLAYER_ACTIVE_TOUCH_MODEL_HPP
#define FOOTBALL_PLAYER_ACTIVE_TOUCH_MODEL_HPP

#include <cmath>

#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_response.hpp"
#include "football/ball/ball_state.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/ball/ball_model.hpp"

// P5c-2: pure active-touch model. The Player chooses *where on the ball* and
// *how hard* to strike; the impulse response derives both velocity and spin.
// There is no angular-velocity assignment path and no SetRotation equivalent.
//
// Coordinate meaning:
//   desired_ball_center - the baked animation's desired ball CENTRE for the
//                         contact frame, used only for reachability.
//   contact_point       - a point on the real ball surface, derived from the
//                         technique offset and then projected onto the sphere.
//   strike direction    - the direction of the required impulse J.
//
// The contact point sits on the near side of the ball (opposite the outgoing
// direction) plus an explicit technique offset. A pure centre strike therefore
// has a lever arm parallel to the impulse and produces no spin; lateral and
// vertical offsets produce side/top spin through ApplyImpulseAtPoint.
namespace football::sim::player {

struct TouchTechnique {
  // Metres off the ball's centre-line, in the strike frame. Zero/zero is a laces
  // strike through the centre and is expected to produce (near) zero spin.
  float lateral = 0.0f;   // >0 to the right of the strike direction
  float vertical = 0.0f;  // >0 above the centre-line
};

enum class TouchRejection { None, Distance, Height, SurfaceRange, Direction };

// Read-only proposal inputs. `passive_endpoint` is the tick's predicted passive
// Ball endpoint; the real Ball is never mutated here.
struct TouchProposalInput {
  football::ball::BallState passive_endpoint;
  football::math::Vector3 desired_ball_center = football::math::Vector3(0);
  football::math::Vector3 target_velocity = football::math::Vector3(0);
  // P4f-3 real boot surface point (world) nearest the ball at the contact
  // frame. A zero `strike_radius` keeps the legacy desired-centre fallback;
  // otherwise reachability and the ball surface contact point come from this
  // geometry and never from an arbitrary projection of a far ball.
  football::math::Vector3 strike_point = football::math::Vector3(0);
  float strike_radius = 0.0f;
  TouchTechnique technique;
  football::ball::BallModel config;
  float reach = 0.4f;
  float height_tolerance = 1.0f;
};

struct TouchProposal {
  TouchRejection rejection = TouchRejection::None;
  bool reachable = false;
  float reach_error = 0.0f;
  float height_gap = 0.0f;
  float surface_error = 0.0f;
  football::math::Vector3 contact_point = football::math::Vector3(0);
  football::math::Vector3 lever_arm = football::math::Vector3(0);
  football::ball::BallImpulse impulse;
  football::ball::BallState response;
  float velocity_error = 0.0f;  // |response.velocity - target_velocity|
  football::math::Vector3 spin = football::math::Vector3(0);  // response angular velocity
};

// Reachability is decided BEFORE any geometric projection, so an out-of-reach
// action is rejected instead of being hidden by a surface projection.
// Ordered gates: aim direction, distance, height, technique surface range.
inline TouchProposal ProposeActiveTouch(const TouchProposalInput& input) {
  TouchProposal out;
  const float radius = input.config.radius();
  const float speed = input.target_velocity.GetLength();
  if (!(speed > 1e-6f)) {
    out.rejection = TouchRejection::Direction;
    return out;
  }
  const auto& ball = input.passive_endpoint.position;
  const bool foot = input.strike_radius > 0;
  if (foot) {
    // Foot-surface to ball-surface gap, measured from the real boot geometry.
    const float foot_dist = (input.strike_point - ball).GetLength();
    out.reach_error = std::max(0.0f, foot_dist - radius - input.strike_radius);
    out.height_gap = std::fabs(input.strike_point.coords[2] - ball.coords[2]);
    if (out.reach_error > input.reach) {
      out.rejection = TouchRejection::Distance;
      return out;
    }
    if (out.height_gap > input.height_tolerance) {
      out.rejection = TouchRejection::Height;
      return out;
    }
  } else {
    out.reach_error = (input.desired_ball_center - ball).GetLength();
    out.height_gap = std::fabs(input.desired_ball_center.coords[2] - ball.coords[2]);
    if (!(out.reach_error < input.reach)) {
      out.rejection = TouchRejection::Distance;
      return out;
    }
    if (!(out.height_gap < input.height_tolerance)) {
      out.rejection = TouchRejection::Height;
      return out;
    }
  }
  // The technique offset cannot exceed the ball silhouette; beyond that the
  // strike point does not exist on the ball.
  const float offset_length =
      std::sqrt(input.technique.lateral * input.technique.lateral +
                input.technique.vertical * input.technique.vertical);
  if (offset_length > radius) {
    out.rejection = TouchRejection::SurfaceRange;
    return out;
  }
  out.impulse.impulse =
      (input.target_velocity - input.passive_endpoint.velocity) * input.config.mass();
  const float impulse_length = out.impulse.impulse.GetLength();
  if (!(impulse_length > 1e-9f)) {
    out.rejection = TouchRejection::Direction;
    return out;
  }
  // The impulse direction is the strike axis. For a real foot the ball contact
  // point is the near-side surface point toward the boot; for the legacy
  // fallback it is the near side opposite the impulse (torque-free centre
  // strike). Lateral/vertical technique offsets then add deliberate spin
  // around that real base point.
  const football::math::Vector3 strike = out.impulse.impulse / impulse_length;
  const football::math::Vector3 up{0, 0, 1};
  football::math::Vector3 side = strike.GetCrossProduct(up);
  side = side.GetNormalized(football::math::Vector3(1, 0, 0));
  const football::math::Vector3 base_dir = foot
      ? (input.strike_point - ball).GetNormalized(up)
      : -strike;
  const football::math::Vector3 offset =
      base_dir * radius + side * input.technique.lateral + up * input.technique.vertical;
  // Guarantee a real surface point while preserving the lever-arm direction.
  out.contact_point = ball + offset.GetNormalized(up) * radius;
  out.lever_arm = out.contact_point - ball;
  out.surface_error = std::fabs(out.lever_arm.GetLength() - radius);
  out.impulse.contact_point = out.contact_point;
  out.response = football::ball::ApplyImpulseAtPoint(
      input.passive_endpoint, out.impulse.impulse, out.contact_point, input.config);
  out.velocity_error = (out.response.velocity - input.target_velocity).GetLength();
  out.spin = out.response.angular_velocity;
  out.reachable = true;
  return out;
}

}  // namespace football::sim::player

#endif
