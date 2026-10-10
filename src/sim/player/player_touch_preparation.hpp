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
  blunted::Vector3 desired_ball_center;
  blunted::Vector3 target_velocity;
  // Possession anchoring is a constraint, never an enormous strike impulse.
  bool retain_anchor = false;
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
