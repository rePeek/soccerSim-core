#ifndef FOOTBALL_PLAYER_ACTIVE_TOUCH_MODEL_HPP
#define FOOTBALL_PLAYER_ACTIVE_TOUCH_MODEL_HPP

#include <cmath>

#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_response.hpp"
#include "football/ball/ball_state.hpp"
#include "foundation/math/vector3.hpp"
#include "model/ball_config.hpp"

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
  blunted::Vector3 desired_ball_center = blunted::Vector3(0);
  blunted::Vector3 target_velocity = blunted::Vector3(0);
  TouchTechnique technique;
  football::model::BallConfig config;
  float reach = 0.4f;
  float height_tolerance = 1.0f;
};

struct TouchProposal {
  TouchRejection rejection = TouchRejection::None;
  bool reachable = false;
  float reach_error = 0.0f;
  float height_gap = 0.0f;
  float surface_error = 0.0f;
  blunted::Vector3 contact_point = blunted::Vector3(0);
  blunted::Vector3 lever_arm = blunted::Vector3(0);
  football::ball::BallImpulse impulse;
  football::ball::BallState response;
  float velocity_error = 0.0f;  // |response.velocity - target_velocity|
  blunted::Vector3 spin = blunted::Vector3(0);  // response angular velocity
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
  out.reach_error = (input.desired_ball_center - input.passive_endpoint.position).GetLength();
  out.height_gap =
      std::fabs(input.desired_ball_center.coords[2] - input.passive_endpoint.position.coords[2]);
  if (!(out.reach_error < input.reach)) {
    out.rejection = TouchRejection::Distance;
    return out;
  }
  if (!(out.height_gap < input.height_tolerance)) {
    out.rejection = TouchRejection::Height;
    return out;
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
  // The contact normal must line up with the impulse, otherwise a centre strike
  // could not be torque-free. A zero-offset strike is therefore spin-free even
  // when the incoming ball is already moving.
  const blunted::Vector3 strike = out.impulse.impulse / impulse_length;
  // Strike frame: `side` is right of the impulse, `up` is world up.
  const blunted::Vector3 up{0, 0, 1};
  blunted::Vector3 side = strike.GetCrossProduct(up);
  side = side.GetNormalized(blunted::Vector3(1, 0, 0));
  const blunted::Vector3 offset =
      -strike * radius + side * input.technique.lateral + up * input.technique.vertical;
  // Guarantee a real surface point while preserving the lever-arm direction.
  out.contact_point = input.passive_endpoint.position + offset.GetNormalized(up) * radius;
  out.lever_arm = out.contact_point - input.passive_endpoint.position;
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
