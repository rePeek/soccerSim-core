#include "player/contact/player_active_touch_model.hpp"

#include <algorithm>
#include <cmath>

namespace football::sim::player {

TouchProposal ProposeActiveTouch(const TouchProposalInput& input) {
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
