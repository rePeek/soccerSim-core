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
TouchProposal ProposeActiveTouch(const TouchProposalInput& input);

}  // namespace football::sim::player

#endif
