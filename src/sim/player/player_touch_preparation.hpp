#ifndef FOOTBALL_PLAYER_TOUCH_PREPARATION_HPP
#define FOOTBALL_PLAYER_TOUCH_PREPARATION_HPP

#include "sim/event/accepted_touch.hpp"
#include "sim/player/player_body_collider_motion.hpp"
#include "football/ball/ball_state.hpp"

namespace football::sim {
// A proposed action is not a rule touch. All actors in the preparation phase
// read the same passive endpoint; no proposal is visible as an accepted touch.
struct PreparedPlayerTouch {
  event::AcceptedTouch fact;
  PlayerBodyPart part = PlayerBodyPart::LowerBody;
  football::math::Vector3 desired_ball_center;
  football::math::Vector3 target_velocity;
  // Possession anchoring is a constraint, never an enormous strike impulse.
  bool retain_anchor = false;
  // P4f-3: real foot geometry captured from the baked pose at the contact
  // frame. `strike_point` is the boot surface point nearest the ball in the
  // tick common frame; `strike_radius` is that boot's radius and doubles as a
  // presence flag (zero => legacy desired-center fallback). `strike_foot` is
  // 0=left, 1=right, -1=unknown. Never confuse this with `desired_ball_center`,
  // which is the animation's intended ball centre, or with the final ball
  // surface contact point computed from `strike_point`.
  football::math::Vector3 strike_point;
  float strike_radius = 0.0f;
  int strike_foot = -1;
  // Provenance sampled during the one real Humanoid pass, not reconstructed later.
  int animation_id = -1, contact_frame = -1, frame = 0;
};
class PlayerTouchPreparationSink {
 public:
  virtual ~PlayerTouchPreparationSink() = default;
  virtual void PrepareTouch(const PreparedPlayerTouch& touch) = 0;
};
} // namespace football::sim
#endif
